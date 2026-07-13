#include "DreamMazeComponent.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "NiceInkCharacter.h"
#include "NiceInkGameState.h"

namespace
{
	constexpr float CheckpointTouchRadius = 0.5f;   // 存檔點觸發半徑（cell）
	constexpr float AwaitRotationFailsafeSec = 10.0f; // 網路失聯保底（僅魯棒性，非設計保護）

	// 視錐（2026-07-13 規格：往移動方向的視錐＋牆擋光，其餘皆黑暗）
	constexpr float ConeHalfRad = 40.0f * PI / 180.0f; // 半角
	constexpr float ConeFadeRad = 14.0f * PI / 180.0f; // 角向邊緣淡出
	constexpr float FeetGlowRadius = 0.3f;             // 腳邊小光圈：小於走廊半寬＝不透牆

	// 導航前瞻（局部貪婪）：刻意不做全域尋路——那會替玩家解迷宮
	constexpr float NavLookahead = 0.9f; // cell
	constexpr float NavMinGain = 0.03f;  // 最低改善門檻（低於此＝已在游標的最近點）

	// 迷宮盤配色（醉夢：深靛底、淡牆）。ShadowColor＝DiscColor 不透明同色——
	// 被遮住的區域塗出來的黑必須和底盤的黑一模一樣，牆後才讀成「黑暗」而非補丁
	const FLinearColor DiscColor(0.045f, 0.04f, 0.10f, 1.0f);
	const FLinearColor ShadowColor(0.045f, 0.04f, 0.10f, 1.0f);
	const FLinearColor WallColor(0.62f, 0.60f, 0.78f, 1.0f);
	const FLinearColor AvatarColor(0.95f, 0.93f, 0.85f, 1.0f);
	const FLinearColor SprayCpColor(0.35f, 0.85f, 0.75f, 1.0f);
	const FLinearColor KickCpColor(0.95f, 0.62f, 0.25f, 1.0f);
	const FLinearColor ExitGlowColor(0.85f, 0.72f, 0.35f, 1.0f);
	const FLinearColor FloorGlowInner(0.30f, 0.28f, 0.44f, 0.32f); // 視錐地面光（扇心）
	const FLinearColor FloorGlowOuter(0.30f, 0.28f, 0.44f, 0.0f);  // 視錐地面光（射程端）
}

UDreamMazeComponent::UDreamMazeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(false); // 迷宮狀態不複製——「其餘玩家一無所知」是規則本體
}

ANiceInkCharacter* UDreamMazeComponent::OwnerChar() const
{
	return Cast<ANiceInkCharacter>(GetOwner());
}

void UDreamMazeComponent::StartMaze(int32 Seed, const FDreamMazeParams& Params, const TArray<int32>& TrapOwnerIds)
{
	FDreamMazeGen::Generate(Params, Seed, TrapOwnerIds.Num(), Layout);
	TrapOwners = TrapOwnerIds;
	if (TrapOwners.Num() > Layout.TrapCells.Num())
	{
		TrapOwners.SetNum(Layout.TrapCells.Num()); // 極端參數保底砍陷阱時對齊
	}
	Net.Build(Layout);
	Occluders.Build(Layout);

	bMazeActive = true;
	SimState = EDreamMazeSimState::Walking;
	StateTime = 0.0f;
	Pos = FVector2D::ZeroVector;
	RespawnPos = FVector2D::ZeroVector;
	HeadingMaze = FVector2D(0.0f, 1.0f);
	DisplayAngleDeg = 0.0f;
	LastKillerId = INDEX_NONE;
	bRotationReceived = false;
	PendingRotationDeg = 0.0f;
	bSprayVisited = bKickVisited = false;
	bInsideSpray = bInsideKick = false;
	bExitSent = false;
	PendingDebugTrap = INDEX_NONE;
	PendingDebugCheckpoint = INDEX_NONE;
	bPendingDebugExit = false;
	bOnRail = false;
	RailIdx = INDEX_NONE;
	RailS = 0.0f;
	CursorPanel = FVector2D(0.0f, -60.0f);
	CursorMazeTarget = FVector2D::ZeroVector;
}

void UDreamMazeComponent::StopMaze()
{
	bMazeActive = false;
	SimState = EDreamMazeSimState::Inactive;
}

void UDreamMazeComponent::ApplyRotation(float AngleDeg)
{
	if (SimState == EDreamMazeSimState::DeathScreen || SimState == EDreamMazeSimState::AwaitRotation)
	{
		PendingRotationDeg = FMath::Clamp(AngleDeg, -360.0f, 360.0f);
		bRotationReceived = true;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DreamMaze: rotation %.0f arrived in state %d, ignored"),
			AngleDeg, static_cast<int32>(SimState));
	}
}

void UDreamMazeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ANiceInkCharacter* C = OwnerChar();
	if (!bMazeActive || !C || !C->IsLocallyControlled() || !C->bAsleep || C->bEyesOpen)
	{
		return;
	}

	StateTime += DeltaTime;

	// 游標與視錐在整個死亡序列中照常活著（2026-07-13 user 定案：旋轉時人物不可移動、
	// 但視錐依然跟著游標轉——人在原點＝旋轉不動點，看著牆型轉是抓方向感的唯一手段）
	if (SimState != EDreamMazeSimState::Inactive)
	{
		UpdateCursorHeading();
	}

	switch (SimState)
	{
	case EDreamMazeSimState::Walking:
		TickWalking(DeltaTime);
		break;

	case EDreamMazeSimState::DeathScreen:
		// 亮兇手名；轉盤等待藏在後面。旋轉可能已先到（buffer 在 bRotationReceived）
		if (StateTime >= Layout.Params.DeathScreenSec)
		{
			SimState = EDreamMazeSimState::AwaitRotation;
			StateTime = 0.0f;
		}
		break;

	case EDreamMazeSimState::AwaitRotation:
		// 人已在原點、視錐可掃——兇手轉盤的等待藏在這段畫面後
		if (bRotationReceived || StateTime > AwaitRotationFailsafeSec)
		{
			if (!bRotationReceived)
			{
				PendingRotationDeg = 0.0f; // 失聯保底＝0 度（SPEC 逾時語意）
			}
			BeginRotationAnim();
		}
		break;

	case EDreamMazeSimState::Rotating:
	{
		const float Duration = FMath::Max(0.5f, Layout.Params.RotAnimDuration);
		const float T = FMath::Min(StateTime / Duration, 1.0f);
		DisplayAngleDeg = EvalRotationAnim(T);
		if (T >= 1.0f)
		{
			DisplayAngleDeg = RotStartAngleDeg + PendingRotationDeg; // 收斂到精確值
			// 人已在死亡當下回到原點（旋轉不動點）；視錐朝向由游標持續驅動，不重置
			bRotationReceived = false;
			SimState = EDreamMazeSimState::Walking;
			StateTime = 0.0f;
		}
		break;
	}

	default:
		break;
	}
}

void UDreamMazeComponent::TickWalking(float DeltaTime)
{
	ANiceInkCharacter* C = OwnerChar();

	// --- robo 除錯觸發（真實事件鏈由此進：位置到位→偵測→RPC） ---
	if (PendingDebugTrap != INDEX_NONE)
	{
		const int32 TrapIndex = PendingDebugTrap;
		PendingDebugTrap = INDEX_NONE;
		if (Layout.TrapCells.IsValidIndex(TrapIndex))
		{
			Pos = Layout.CellCenter(Layout.TrapCells[TrapIndex]);
			bOnRail = false; // 傳送＝軌道狀態失效；死亡重生時 PlaceAtCell 重掛
			HandleTrapTouched(TrapIndex);
			return;
		}
	}
	if (PendingDebugCheckpoint != INDEX_NONE)
	{
		const int32 Type = PendingDebugCheckpoint;
		PendingDebugCheckpoint = INDEX_NONE;
		const int32 Cell = (Type == 0) ? Layout.CheckpointSprayCell : Layout.CheckpointKickCell;
		if (Cell != INDEX_NONE)
		{
			PlaceAtCell(Cell);
			HandleCheckpointTouched(Type);
		}
	}
	if (bPendingDebugExit)
	{
		bPendingDebugExit = false;
		if (Layout.ExitCell != INDEX_NONE)
		{
			Pos = Layout.CellCenter(Layout.ExitCell);
		}
		TriggerExit();
		return;
	}

	// --- 走路（游標/視錐更新在 UpdateCursorHeading，死亡序列也活著；這裡只管移動）：
	//     按住左鍵才走路（旋轉/死亡序列中本函式不會被呼叫＝人物鎖定不可移動） ---
	APlayerController* PC = C ? Cast<APlayerController>(C->GetController()) : nullptr;
	if (PC && PC->IsInputKeyDown(EKeys::LeftMouseButton))
	{
		const float DistTarget = (CursorMazeTarget - Pos).Size();
		if (DistTarget > 0.12f)
		{
			const float MoveDist = Layout.Params.AvatarSpeed * DeltaTime;
			if (bOnRail)
			{
				RailNavigate(CursorMazeTarget, MoveDist);
			}
			else
			{
				FreeMove(CursorMazeTarget, MoveDist);
			}
			if (bExitSent)
			{
				return; // 走出出口
			}
		}
	}

	// --- 陷阱（固定、可重複觸發；地圖上完全不可見——2026-07-13 定案） ---
	for (int32 TrapIndex = 0; TrapIndex < Layout.TrapCells.Num(); ++TrapIndex)
	{
		if ((Pos - Layout.CellCenter(Layout.TrapCells[TrapIndex])).Size() < Layout.Params.TrapTriggerRadius)
		{
			HandleTrapTouched(TrapIndex);
			return;
		}
	}

	// --- 存檔點（進入事件；重複經過＝重新存檔，技能授予由 server 每回合一次去重） ---
	if (Layout.CheckpointSprayCell != INDEX_NONE)
	{
		const bool bInside = (Pos - Layout.CellCenter(Layout.CheckpointSprayCell)).Size() < CheckpointTouchRadius;
		if (bInside && !bInsideSpray)
		{
			HandleCheckpointTouched(0);
		}
		bInsideSpray = bInside;
	}
	if (Layout.CheckpointKickCell != INDEX_NONE)
	{
		const bool bInside = (Pos - Layout.CellCenter(Layout.CheckpointKickCell)).Size() < CheckpointTouchRadius;
		if (bInside && !bInsideKick)
		{
			HandleCheckpointTouched(1);
		}
		bInsideKick = bInside;
	}
}

void UDreamMazeComponent::HandleTrapTouched(int32 TrapIndex)
{
	if (SimState != EDreamMazeSimState::Walking)
	{
		return;
	}
	LastKillerId = TrapOwners.IsValidIndex(TrapIndex) ? TrapOwners[TrapIndex] : INDEX_NONE;
	if (ANiceInkCharacter* C = OwnerChar())
	{
		C->ServerMazeTrapHit(LastKillerId);
	}
	// 2026-07-13 user 定案：被抓到一律回原點，回到原點之後才開始旋轉。
	// 原點＝圓盤旋轉不動點——圓點旋轉中不必再隱藏（位置洩漏不了度數）
	PlaceAtCell(0);
	SimState = EDreamMazeSimState::DeathScreen;
	StateTime = 0.0f;
	bRotationReceived = false;
}

void UDreamMazeComponent::HandleCheckpointTouched(int32 CheckpointType)
{
	// 2026-07-13 user 定案：存檔點概念取消——這裡是純技能拾取點，不再更新重生位置
	bool& bVisited = (CheckpointType == 0) ? bSprayVisited : bKickVisited;
	if (!bVisited)
	{
		bVisited = true;
		if (ANiceInkCharacter* C = OwnerChar())
		{
			C->ServerMazeCheckpointReached(static_cast<uint8>(CheckpointType));
		}
	}
}

void UDreamMazeComponent::TriggerExit()
{
	if (bExitSent)
	{
		return;
	}
	// 出口只在作畫階段有效（Seating 的入座演出秒數內走不完；防禦性擋掉）
	const ANiceInkGameState* GS = GetWorld() ? GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	if (!GS || GS->CurrentPhase != ENiceInkPhase::Drawing)
	{
		return;
	}
	if (ANiceInkCharacter* C = OwnerChar())
	{
		C->ServerMazeExited();
		bExitSent = true;
	}
}

// --- 導航式移動（2026-07-13）：局部前瞻貪婪。玩家大概指出方向就能走——
// 斜指牆壁＝沿廊滑行是「更接近游標」的最優走法、自動發生；指進岔路＝自動轉彎。
// 刻意不做全域尋路：只有 local 看得到的「更近」才走，路仍然要玩家自己找。 ---

void UDreamMazeComponent::PlaceAtCell(int32 Cell)
{
	if (Cell <= 0 || Cell == INDEX_NONE)
	{
		bOnRail = false;
		RailIdx = INDEX_NONE;
		RailS = 0.0f;
		Pos = FVector2D::ZeroVector; // 中心圓室＝自由移動區
		return;
	}
	Pos = Layout.CellCenter(Cell);
	if (const TArray<FDreamMazeRailRef>* Refs = Net.NodeRails.Find(Cell))
	{
		if (Refs->Num() > 0)
		{
			const FDreamMazeRailRef& Ref = (*Refs)[0];
			bOnRail = true;
			RailIdx = Ref.RailIdx;
			RailS = Ref.bEndA ? 0.0f : Net.Rails[Ref.RailIdx].Len;
			return;
		}
	}
	bOnRail = false;
}

void UDreamMazeComponent::UpdateCursorHeading()
{
	// 滑鼠增量推虛擬游標；按住右鍵＝瞄準模式（滑鼠讓回轉頭——PollLook 同一約定）。
	// 死亡序列中也照常執行（2026-07-13：旋轉中不可移動、但視錐跟游標）
	ANiceInkCharacter* C = OwnerChar();
	APlayerController* PC = C ? Cast<APlayerController>(C->GetController()) : nullptr;
	if (!PC)
	{
		return;
	}
	if (!PC->IsInputKeyDown(EKeys::RightMouseButton))
	{
		float MouseX = 0.0f;
		float MouseY = 0.0f;
		PC->GetInputMouseDelta(MouseX, MouseY);
		const float Lim = FMath::Max(80.0f, LastPanelRadius * 1.15f);
		CursorPanel.X = FMath::Clamp(CursorPanel.X + MouseX * MazeCursorSensitivity, -Lim, Lim);
		CursorPanel.Y = FMath::Clamp(CursorPanel.Y - MouseY * MazeCursorSensitivity, -Lim, Lim); // 螢幕 y 向下
	}
	// 游標（盤面像素）→ 迷宮座標：除以縮放、y 翻轉、顯示旋轉逆變換——
	// 被轉之後玩家操縱的是「他看到的」；旋轉動畫中每幀用當下角度重解＝視錐黏著螢幕游標
	const float Scale = FMath::Max(1.0f, LastPanelScale);
	CursorMazeTarget = FVector2D(CursorPanel.X / Scale, -CursorPanel.Y / Scale).GetRotated(-DisplayAngleDeg);
	const FVector2D ToTarget = CursorMazeTarget - Pos;
	const float DistTarget = ToTarget.Size();
	if (DistTarget > 0.12f)
	{
		HeadingMaze = ToTarget / DistTarget; // 走路時 RailNavigate 會改成實際行進方向
	}
}

void UDreamMazeComponent::FreeMove(const FVector2D& Target, float Dist)
{
	FVector2D Dir = Target - Pos;
	const float L = Dir.Size();
	if (L < 0.08f)
	{
		return;
	}
	Dir /= L;
	const float Step = FMath::Min(Dist, L);
	FVector2D Cand = Pos + Dir * Step;
	const float R = Cand.Size();
	if (R > Net.FreeR)
	{
		Cand *= Net.FreeR / R; // 撞自由區邊界＝沿圓弧滑
	}
	Pos = Cand;
	// 門口捕捉：貼近門且指向大致朝外 → 上軌（指沿邊方向＝繼續滑去別的門）
	for (int32 DoorRail = 0; DoorRail < Net.Rails.Num(); ++DoorRail)
	{
		const FDreamMazeRail& Rail = Net.Rails[DoorRail];
		if (Rail.EndA.Type != EDreamMazeRailEnd::Free)
		{
			continue;
		}
		const FVector2D DoorPt = Rail.Pts[0];
		if ((Pos - DoorPt).Size() < 0.35f)
		{
			FVector2D SmpPos, SmpTan;
			Rail.Sample(0.001f, SmpPos, SmpTan);
			if (FVector2D::DotProduct(Dir, SmpTan) > 0.05f)
			{
				bOnRail = true;
				RailIdx = DoorRail;
				RailS = 0.0f;
				Pos = DoorPt;
				return;
			}
		}
	}
}

float UDreamMazeComponent::RailLookahead(int32 Ri, float S, int32 Dir, const FVector2D& Target, float La, int32 Depth) const
{
	const FDreamMazeRail& Rail = Net.Rails[Ri];
	float Best = BIG_NUMBER;
	const float Reach = Dir > 0 ? Rail.Len - S : S;
	const float Walk = FMath::Min(La, Reach);
	constexpr int32 NumSamples = 6;
	FVector2D SmpPos, SmpTan;
	for (int32 i = 1; i <= NumSamples; ++i)
	{
		Rail.Sample(S + Dir * Walk * i / NumSamples, SmpPos, SmpTan);
		Best = FMath::Min(Best, (SmpPos - Target).Size());
	}
	if (Walk < 1e-6f)
	{
		Rail.Sample(S, SmpPos, SmpTan);
		Best = (SmpPos - Target).Size();
	}
	const float Rem = La - Walk;
	if (Rem > 0.08f)
	{
		const FDreamMazeRailEndpoint& End = Dir > 0 ? Rail.EndB : Rail.EndA;
		if (End.Type == EDreamMazeRailEnd::Node && Depth > 0)
		{
			if (const TArray<FDreamMazeRailRef>* Refs = Net.NodeRails.Find(End.Cell))
			{
				for (const FDreamMazeRailRef& Ref : *Refs)
				{
					if (Ref.RailIdx == Ri)
					{
						continue; // 回頭路由對向選項涵蓋
					}
					const FDreamMazeRail& NextRail = Net.Rails[Ref.RailIdx];
					Best = FMath::Min(Best, RailLookahead(Ref.RailIdx,
						Ref.bEndA ? 0.0f : NextRail.Len, Ref.bEndA ? 1 : -1, Target, Rem, Depth - 1));
				}
			}
		}
		else if (End.Type == EDreamMazeRailEnd::Exit || End.Type == EDreamMazeRailEnd::Free)
		{
			Rail.Sample(Dir > 0 ? Rail.Len : 0.0f, SmpPos, SmpTan);
			float V = (SmpPos - Target).Size();
			if (End.Type == EDreamMazeRailEnd::Free)
			{
				// 自由區受圓形邊界鉗位：實際可貼近下限＝|Target|−FreeR
				// （不鉗位＝高估收益→門口進出跳針，網頁原型踩過的雷）
				V = FMath::Max(FMath::Max(0.0f, Target.Size() - Net.FreeR), V - Rem);
			}
			Best = FMath::Min(Best, V);
		}
	}
	return Best;
}

void UDreamMazeComponent::RailNavigate(const FVector2D& Target, float Dist)
{
	int32 Guard = 0;
	while (Dist > 1e-5f && Guard++ < 30)
	{
		const FDreamMazeRail& Rail = Net.Rails[RailIdx];
		FVector2D Here, HereTan;
		Rail.Sample(RailS, Here, HereTan);
		const float D0 = (Here - Target).Size();
		if (D0 < 0.1f)
		{
			return; // 已站在游標腳下
		}

		int32 BestMoveDir = 0;
		FDreamMazeRailRef BestJump;
		bool bJump = false;
		bool bGoFree = false;
		float BestGain = NavMinGain;
		for (int32 Dir = 1; Dir >= -1; Dir -= 2)
		{
			const float Reach = Dir > 0 ? Rail.Len - RailS : RailS;
			if (Reach > 1e-6f)
			{
				const float Gain = D0 - RailLookahead(RailIdx, RailS, Dir, Target, NavLookahead, 2);
				if (Gain > BestGain)
				{
					BestGain = Gain;
					BestMoveDir = Dir;
					bJump = false;
					bGoFree = false;
				}
			}
			else
			{
				const FDreamMazeRailEndpoint& End = Dir > 0 ? Rail.EndB : Rail.EndA;
				if (End.Type == EDreamMazeRailEnd::Node)
				{
					if (const TArray<FDreamMazeRailRef>* Refs = Net.NodeRails.Find(End.Cell))
					{
						for (const FDreamMazeRailRef& Ref : *Refs)
						{
							if (Ref.RailIdx == RailIdx)
							{
								continue;
							}
							const FDreamMazeRail& NextRail = Net.Rails[Ref.RailIdx];
							if (NextRail.Len < 1e-6f)
							{
								continue;
							}
							const float Gain = D0 - RailLookahead(Ref.RailIdx,
								Ref.bEndA ? 0.0f : NextRail.Len, Ref.bEndA ? 1 : -1, Target, NavLookahead, 2);
							if (Gain > BestGain)
							{
								BestGain = Gain;
								BestJump = Ref;
								bJump = true;
								bGoFree = false;
								BestMoveDir = 0;
							}
						}
					}
				}
				else if (End.Type == EDreamMazeRailEnd::Free)
				{
					// 退回中央房的收益＝鉗位後真正能貼近游標多少
					const float BestFree = FMath::Max(FMath::Max(0.0f, Target.Size() - Net.FreeR), D0 - NavLookahead);
					const float Gain = D0 - BestFree;
					if (Gain > BestGain)
					{
						BestGain = Gain;
						bGoFree = true;
						bJump = false;
						BestMoveDir = 0;
					}
				}
			}
		}

		if (bJump)
		{
			// 站在路口換軌（不耗距離；下一輪立即沿新軌前進）
			RailS = BestJump.bEndA ? 0.0f : Net.Rails[BestJump.RailIdx].Len;
			RailIdx = BestJump.RailIdx;
			continue;
		}
		if (bGoFree)
		{
			bOnRail = false;
			FreeMove(Target, Dist);
			return;
		}
		if (BestMoveDir == 0)
		{
			return; // 沒有任何走法更接近游標＝這裡就是你指的地方的最近點
		}

		const float Reach = BestMoveDir > 0 ? Rail.Len - RailS : RailS;
		const float Step = FMath::Min(Dist, Reach);
		RailS += BestMoveDir * Step;
		Dist -= Step;
		FVector2D SmpPos, SmpTan;
		Rail.Sample(RailS, SmpPos, SmpTan);
		Pos = SmpPos;
		HeadingMaze = SmpTan * static_cast<float>(BestMoveDir);
		if (Step >= Reach - 1e-6f)
		{
			const FDreamMazeRailEndpoint& End = BestMoveDir > 0 ? Rail.EndB : Rail.EndA;
			if (End.Type == EDreamMazeRailEnd::Exit)
			{
				TriggerExit();
				return;
			}
			if (End.Type == EDreamMazeRailEnd::Free)
			{
				bOnRail = false;
				if (Dist > 1e-5f)
				{
					FreeMove(Target, Dist);
				}
				return;
			}
			// Node → 下一輪展開路口選項
		}
	}
}

void UDreamMazeComponent::BeginRotationAnim()
{
	SimState = EDreamMazeSimState::Rotating;
	StateTime = 0.0f;
	RotStartAngleDeg = DisplayAngleDeg;

	// 晃動曲線：端點歸零（結束＝精確目標）、隨機頻率/相位（不可估計）、
	// 振幅受角速度上限約束（定位點盯得住）。0 度也走同一條路——不可分辨。
	const FDreamMazeParams& P = Layout.Params;
	const int32 Octaves = FMath::Clamp(P.WobbleOctaves, 1, 4);
	WobbleFreq.Reset();
	WobblePhase.Reset();
	float WobblePeakSpeed = 0.0f; // 每單位振幅的峰值角速度（度/秒）
	for (int32 i = 0; i < Octaves; ++i)
	{
		const float Freq = 0.6f + 0.55f * i + FMath::FRand() * 0.4f;
		WobbleFreq.Add(Freq);
		WobblePhase.Add(FMath::FRand() * 2.0f * PI);
		WobblePeakSpeed += 2.0f * PI * Freq / FMath::Max(0.5f, P.RotAnimDuration);
	}
	const float PerOctaveAmp = P.WobbleAmplitudeDeg / Octaves;
	const float BasePeakSpeed = FMath::Abs(PendingRotationDeg) * 1.5f / FMath::Max(0.5f, P.RotAnimDuration);
	const float Budget = FMath::Max(0.0f, P.MaxAngularSpeedDeg - BasePeakSpeed);
	const float MaxAmp = WobblePeakSpeed > 1.0f ? Budget / WobblePeakSpeed : PerOctaveAmp;
	WobbleAmp = FMath::Min(PerOctaveAmp, MaxAmp);
}

float UDreamMazeComponent::EvalRotationAnim(float NormalizedT) const
{
	const float Base = FMath::SmoothStep(0.0f, 1.0f, NormalizedT);
	float Angle = RotStartAngleDeg + PendingRotationDeg * Base;
	const float Window = FMath::Sin(PI * NormalizedT); // 端點歸零
	for (int32 i = 0; i < WobbleFreq.Num(); ++i)
	{
		Angle += WobbleAmp * FMath::Sin(2.0f * PI * WobbleFreq[i] * NormalizedT + WobblePhase[i]) * Window;
	}
	return Angle;
}

// --- 除錯 ---

void UDreamMazeComponent::DebugTriggerTrap(int32 TrapIndex)
{
	PendingDebugTrap = TrapIndex;
}

void UDreamMazeComponent::DebugTriggerCheckpoint(int32 CheckpointType)
{
	PendingDebugCheckpoint = FMath::Clamp(CheckpointType, 0, 1);
}

void UDreamMazeComponent::DebugTriggerExit()
{
	bPendingDebugExit = true;
}

FString UDreamMazeComponent::GetDebugSummary() const
{
	static const TCHAR* StateNames[] = { TEXT("Inactive"), TEXT("Walking"), TEXT("DeathScreen"), TEXT("AwaitRotation"), TEXT("Rotating") };
	FString Traps;
	for (int32 i = 0; i < Layout.TrapCells.Num(); ++i)
	{
		const FVector2D TrapCenter = Layout.CellCenter(Layout.TrapCells[i]);
		Traps += FString::Printf(TEXT("%s(%.2f,%.2f)owner=%d"), i > 0 ? TEXT(";") : TEXT(""),
			TrapCenter.X, TrapCenter.Y, TrapOwners.IsValidIndex(i) ? TrapOwners[i] : INDEX_NONE);
	}
	const FVector2D SprayCp = Layout.CheckpointSprayCell != INDEX_NONE ? Layout.CellCenter(Layout.CheckpointSprayCell) : FVector2D::ZeroVector;
	const FVector2D KickCp = Layout.CheckpointKickCell != INDEX_NONE ? Layout.CellCenter(Layout.CheckpointKickCell) : FVector2D::ZeroVector;
	const FVector2D ExitCenter = Layout.ExitCell != INDEX_NONE ? Layout.CellCenter(Layout.ExitCell) : FVector2D::ZeroVector;
	return FString::Printf(
		TEXT("active=%d state=%s pos=(%.2f,%.2f) angle=%.1f respawn=(%.2f,%.2f) cells=%d solve=%d ideal=%.1fs regen=%d traps=[%s] cpS=(%.2f,%.2f) cpK=(%.2f,%.2f) exit=(%.2f,%.2f) visitedS=%d visitedK=%d exitSent=%d killer=%d"),
		bMazeActive ? 1 : 0,
		StateNames[FMath::Clamp(static_cast<int32>(SimState), 0, 4)],
		Pos.X, Pos.Y, DisplayAngleDeg, RespawnPos.X, RespawnPos.Y,
		Layout.NumCells(), Layout.SafeSolveLen, Layout.IdealSolveSec, Layout.RegenAttempts,
		*Traps, SprayCp.X, SprayCp.Y, KickCp.X, KickCp.Y, ExitCenter.X, ExitCenter.Y,
		bSprayVisited ? 1 : 0, bKickVisited ? 1 : 0, bExitSent ? 1 : 0, LastKillerId);
}

// --- 繪製 ---

void UDreamMazeComponent::DrawMazePanel(UCanvas* Canvas, const FVector2D& CenterPx, float RadiusPx)
{
	if (!Canvas || !bMazeActive || Layout.NumCells() == 0)
	{
		return;
	}

	const FDreamMazeParams& P = Layout.Params;
	const float Scale = RadiusPx / FMath::Max(1.0f, Layout.RimRadius());
	LastPanelCenter = CenterPx; // 輸入端（TickWalking）用同一套盤面轉換解游標
	LastPanelRadius = RadiusPx;
	LastPanelScale = Scale;

	const bool bWalking = SimState == EDreamMazeSimState::Walking;
	// 死亡當下人已回原點（Pos＝旋轉不動點）；視錐朝向全程由游標驅動（含旋轉中）
	const FVector2D Eye = Pos;
	const FVector2D EyeHd = HeadingMaze;
	const float Range = P.VisionRadius;

	// 每幀粗篩一次射程內的遮光段——之後所有射線/LOS 只掃這份（掉幀修正 2026-07-13）
	TArray<int32> CulledOccluders;
	Occluders.CullAround(Eye, Range, CulledOccluders);

	auto ToPx = [&](const FVector2D& MazePos) -> FVector2D
	{
		const FVector2D Rotated = MazePos.GetRotated(DisplayAngleDeg);
		return FVector2D(CenterPx.X + Rotated.X * Scale, CenterPx.Y - Rotated.Y * Scale);
	};

	// 可見度＝純解析視錐（徑向淡出 × 角向淡出；腳邊小光圈只做徑向）。
	// 2026-07-13 user 定案改制：不再逐點問 LOS——視錐內容嚴格照畫，
	// 「牆擋光」由後面的陰影帔（shadow-casting 四邊形）一次塗黑牆後區域。
	// 逐段 LOS 的病：取樣粒度＝明暗跳段＝不完整的牆；陰影帔的明暗邊界是幾何精確的。
	const float CosIn = FMath::Cos(ConeHalfRad);
	const float CosOut = FMath::Cos(ConeHalfRad + ConeFadeRad);
	auto ConeVis = [&](const FVector2D& Pt) -> float
	{
		const FVector2D DVec = Pt - Eye;
		const float Dist = DVec.Size();
		const float Radial = 1.0f - FMath::Clamp(
			(Dist - (Range - P.FogEdgeSoftness)) / FMath::Max(0.1f, P.FogEdgeSoftness), 0.0f, 1.0f);
		if (Radial <= 0.0f)
		{
			return 0.0f;
		}
		if (Dist <= FeetGlowRadius)
		{
			return Radial;
		}
		const float CosA = FVector2D::DotProduct(DVec / Dist, EyeHd);
		const float Ang = FMath::Clamp((CosA - CosOut) / FMath::Max(1e-4f, CosIn - CosOut), 0.0f, 1.0f);
		return Radial * Ang;
	};

	// 底盤（近全黑的夢）
	Canvas->K2_DrawPolygon(nullptr, CenterPx, FVector2D(RadiusPx + 8.0f, RadiusPx + 8.0f), 48, DiscColor);

	// --- v4（2026-07-13 user 模型逐字版）：FPS 嚴格視線呈現＝三層——
	// ①視錐地面光（你能看的空間範圍）
	// ②黑影：每段牆把背後區域投影塗黑（幾何精確＝「被遮住的區域整個塗黑」）
	// ③牆畫在黑影之上：視線碰得到正面的牆「整件完好呈現」（二值物件），
	//   看不到的牆從不上畫面。擋人的牆因此永遠完整，牆後的一切永遠在黑暗裡。 ---

	// ①視錐地面光（解析扇形＋腳邊小光圈；牆後由②塗黑）
	if (SimState != EDreamMazeSimState::Inactive)
	{
		const float Alpha0 = FMath::Atan2(EyeHd.Y, EyeHd.X);
		const float WHalf = ConeHalfRad + ConeFadeRad;
		constexpr int32 NumRays = 48;
		const FVector2D EyePx = ToPx(Eye);
		TArray<FCanvasUVTri> Tris;
		Tris.Reserve(NumRays + 16);
		for (int32 Ray = 1; Ray <= NumRays; ++Ray)
		{
			const float A0 = Alpha0 - WHalf + 2.0f * WHalf * (Ray - 1) / NumRays;
			const float A1 = Alpha0 - WHalf + 2.0f * WHalf * Ray / NumRays;
			FCanvasUVTri Tri;
			Tri.V0_Pos = EyePx;
			Tri.V1_Pos = ToPx(Eye + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * Range);
			Tri.V2_Pos = ToPx(Eye + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * Range);
			Tri.V0_Color = FloorGlowInner;
			Tri.V1_Color = Tri.V2_Color = FloorGlowOuter;
			Tris.Add(Tri);
		}
		// 腳邊小光圈（半徑小於走廊半寬＝物理上照不到任何牆後）
		constexpr int32 FeetSegs = 16;
		for (int32 i = 0; i < FeetSegs; ++i)
		{
			const float A0 = 2.0f * PI * i / FeetSegs;
			const float A1 = 2.0f * PI * (i + 1) / FeetSegs;
			FCanvasUVTri Tri;
			Tri.V0_Pos = EyePx;
			Tri.V1_Pos = ToPx(Eye + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * FeetGlowRadius);
			Tri.V2_Pos = ToPx(Eye + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * FeetGlowRadius);
			Tri.V0_Color = FloorGlowInner;
			Tri.V1_Color = Tri.V2_Color = FloorGlowOuter;
			Tris.Add(Tri);
		}
		Canvas->K2_DrawTriangle(nullptr, Tris); // nullptr＝引擎白紋理
	}

	// ②黑影：被遮住的區域整片塗黑（自牆中線向外投影到射程外；
	// 擋人的牆本體會在③重畫於影子之上——所以這裡蓋到它也無妨）
	{
		TArray<FCanvasUVTri> ShadowTris;
		ShadowTris.Reserve(CulledOccluders.Num() * 2);
		const float ProjDist = Range + 0.5f;
		for (const int32 SegIdx : CulledOccluders)
		{
			const FDreamMazeOccluders::FSeg& Seg = Occluders.Segs[SegIdx];
			const FVector2D DA = Seg.A - Eye;
			const FVector2D DB = Seg.B - Eye;
			const float LenA = DA.Size();
			const float LenB = DB.Size();
			if (LenA < 1e-4f || LenB < 1e-4f || (LenA > Range && LenB > Range))
			{
				continue; // 整段在射程外＝背後本來就黑
			}
			const FVector2D FarA = Eye + DA / LenA * ProjDist;
			const FVector2D FarB = Eye + DB / LenB * ProjDist;
			FCanvasUVTri Tri1;
			Tri1.V0_Pos = ToPx(Seg.A);
			Tri1.V1_Pos = ToPx(Seg.B);
			Tri1.V2_Pos = ToPx(FarB);
			Tri1.V0_Color = Tri1.V1_Color = Tri1.V2_Color = ShadowColor;
			FCanvasUVTri Tri2;
			Tri2.V0_Pos = ToPx(Seg.A);
			Tri2.V1_Pos = ToPx(FarB);
			Tri2.V2_Pos = ToPx(FarA);
			Tri2.V0_Color = Tri2.V1_Color = Tri2.V2_Color = ShadowColor;
			ShadowTris.Add(Tri1);
			ShadowTris.Add(Tri2);
		}
		if (ShadowTris.Num() > 0)
		{
			Canvas->K2_DrawTriangle(nullptr, ShadowTris);
		}
	}

	// ③牆：以「邏輯牆件」為單位二值呈現——正面任一點視線可達＝整件完好畫出
	// （蓋在黑影之上）；否則整件不畫（留在黑暗）。亮度沿牆身按視錐解析淡出；
	// 件端蓋圓頭＝與全圖渲染同一視覺語彙（也修掉尖銳尾端）
	const float WallPx = FMath::Max(2.0f, DreamMazeWallThickness * Scale);
	const float CapR = WallPx * 0.5f;
	for (const FIntPoint& Piece : Occluders.PieceRanges)
	{
		bool bNear = false;
		for (int32 i = Piece.X; i < Piece.Y && !bNear; ++i)
		{
			const FDreamMazeOccluders::FSeg& S = Occluders.Segs[i];
			const float MaxD = Range + S.HalfLen + 0.3f;
			bNear = (S.Mid - Eye).SizeSquared() < MaxD * MaxD;
		}
		if (!bNear)
		{
			continue;
		}
		bool bSeen = false;
		for (int32 i = Piece.X; i < Piece.Y && !bSeen; ++i)
		{
			const FDreamMazeOccluders::FSeg& S = Occluders.Segs[i];
			bSeen = ConeVis(S.Mid) > 0.02f && Occluders.HasLineOfSight(Eye, S.Mid, 0.12f, &CulledOccluders);
		}
		if (!bSeen)
		{
			continue;
		}
		for (int32 i = Piece.X; i < Piece.Y; ++i)
		{
			const FDreamMazeOccluders::FSeg& S = Occluders.Segs[i];
			const float Vis = FMath::Max3(ConeVis(S.Mid), ConeVis(S.A), ConeVis(S.B));
			if (Vis <= 0.02f)
			{
				continue;
			}
			FLinearColor Color = WallColor;
			Color.A *= Vis;
			Canvas->K2_DrawLine(ToPx(S.A), ToPx(S.B), WallPx, Color);
			if (i == Piece.X)
			{
				Canvas->K2_DrawPolygon(nullptr, ToPx(S.A), FVector2D(CapR, CapR), 10, Color);
			}
			if (i == Piece.Y - 1)
			{
				Canvas->K2_DrawPolygon(nullptr, ToPx(S.B), FVector2D(CapR, CapR), 10, Color);
			}
		}
	}

	// 出口光（視線連得到才看得到——黑暗中零穿牆資訊）
	{
		const float Rim = Layout.RimRadius();
		const float ExitPhis[2] = { Layout.ExitPhi0, Layout.ExitPhi1 };
		for (const float Phi : ExitPhis)
		{
			const FVector2D Pt(Rim * FMath::Cos(Phi), Rim * FMath::Sin(Phi));
			const float Vis = ConeVis(Pt);
			if (Vis > 0.03f && Occluders.HasLineOfSight(Eye, Pt, 0.10f, &CulledOccluders))
			{
				FLinearColor Color = ExitGlowColor;
				Color.A *= Vis;
				Canvas->K2_DrawPolygon(nullptr, ToPx(Pt), FVector2D(0.1f * Scale, 0.1f * Scale), 8, Color);
			}
		}
	}

	// 存檔點：視錐照到才可見（v3.3 補位設計「穿霧常駐」廢止——黑暗中零地標，
	// 否則兩個固定圖示＋一次旋轉＝廉價反推度數）
	auto DrawCheckpoint = [&](int32 Cell, const FLinearColor& BaseColor, bool bVisited, const TCHAR* Label)
	{
		if (Cell == INDEX_NONE)
		{
			return;
		}
		const FVector2D Center = Layout.CellCenter(Cell);
		const float Alpha = ConeVis(Center);
		if (Alpha <= 0.03f || !Occluders.HasLineOfSight(Eye, Center, 0.10f, &CulledOccluders))
		{
			return; // 視線被牆擋住＝不畫（逐視線精算，不是畫了再蓋）
		}
		const FVector2D Px = ToPx(Center);
		const float HalfSize = 0.24f * Scale;
		FLinearColor Color = BaseColor;
		Color.A *= Alpha;
		const FVector2D Up(0.0f, -HalfSize), Right(HalfSize, 0.0f);
		Canvas->K2_DrawLine(Px + Up, Px + Right, 2.0f, Color);
		Canvas->K2_DrawLine(Px + Right, Px - Up, 2.0f, Color);
		Canvas->K2_DrawLine(Px - Up, Px - Right, 2.0f, Color);
		Canvas->K2_DrawLine(Px - Right, Px + Up, 2.0f, Color);
		if (bVisited)
		{
			Canvas->K2_DrawPolygon(nullptr, Px, FVector2D(HalfSize * 0.8f, HalfSize * 0.8f), 4, Color);
		}
		FCanvasTextItem Text(Px + FVector2D(-4.0f, HalfSize + 2.0f), FText::FromString(Label), GEngine->GetSmallFont(), Color);
		Text.Scale = FVector2D(0.8f, 0.8f);
		Canvas->DrawItem(Text);
	};
	DrawCheckpoint(Layout.CheckpointSprayCell, SprayCpColor, bSprayVisited, TEXT("S"));
	DrawCheckpoint(Layout.CheckpointKickCell, KickCpColor, bKickVisited, TEXT("K"));

	// 陷阱：不畫。2026-07-13 定案——力士陷阱在地圖上完全不可見，哪裡不能走用命記。
	//（v3.3 待定 #13 的 TellRange 破綻顯形隨此廢止）

	// avatar＋游標全程在場（2026-07-13：重生一律原點＝旋轉不動點，圓點不再洩漏度數）；
	// 人物→游標虛線只在可走路時畫（＝「按左鍵能動」的視覺訊號）
	{
		const FVector2D AvatarPx = ToPx(Pos);
		const FVector2D CursorPx = CenterPx + CursorPanel;

		if (bWalking)
		{
			APlayerController* PC = OwnerChar() ? Cast<APlayerController>(OwnerChar()->GetController()) : nullptr;
			const bool bWalkHeld = PC && PC->IsInputKeyDown(EKeys::LeftMouseButton);
			const FVector2D Span = CursorPx - AvatarPx;
			const float SpanLen = Span.Size();
			if (SpanLen > 4.0f)
			{
				const FVector2D SpanDir = Span / SpanLen;
				FLinearColor DashColor = AvatarColor;
				DashColor.A = bWalkHeld ? 0.55f : 0.22f;
				for (float DashS = 0.0f; DashS < SpanLen; DashS += 14.0f)
				{
					Canvas->K2_DrawLine(AvatarPx + SpanDir * DashS,
						AvatarPx + SpanDir * FMath::Min(SpanLen, DashS + 7.0f), 1.5f, DashColor);
				}
			}
		}
		// 游標十字
		FLinearColor CrossColor = AvatarColor;
		CrossColor.A = 0.85f;
		Canvas->K2_DrawLine(CursorPx - FVector2D(6.0f, 0.0f), CursorPx + FVector2D(6.0f, 0.0f), 1.5f, CrossColor);
		Canvas->K2_DrawLine(CursorPx - FVector2D(0.0f, 6.0f), CursorPx + FVector2D(0.0f, 6.0f), 1.5f, CrossColor);
		// avatar 本體＋朝向短刺
		Canvas->K2_DrawPolygon(nullptr, AvatarPx, FVector2D(4.5f, 4.5f), 8, AvatarColor);
		Canvas->K2_DrawLine(AvatarPx, ToPx(Pos + HeadingMaze * 0.35f), 2.0f, AvatarColor);
	}
}
