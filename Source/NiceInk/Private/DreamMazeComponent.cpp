#include "DreamMazeComponent.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "NiceInkCharacter.h"
#include "NiceInkGameInstance.h"
#include "NiceInkGameState.h"
#include "NiceInkHUD.h"

namespace
{
	constexpr float CheckpointTouchRadius = 0.5f;   // 存檔點觸發半徑（cell）
	constexpr float AwaitRotationFailsafeSec = 10.0f; // 網路失聯保底（僅魯棒性，非設計保護）

	// 導航前瞻（局部貪婪）：刻意不做全域尋路——那會替玩家解迷宮
	constexpr float NavLookahead = 0.9f; // cell
	constexpr float NavMinGain = 0.03f;  // 最低改善門檻（低於此＝已在游標的最近點）

	// 迷宮盤配色（醉夢：近黑底、淡牆）。全部以 sRGB 顯示色（網頁設計台的 hex）宣告、
	// 經 FromSRGBColor 轉線性——canvas 上屏會做 gamma 校正，把顯示值直接塞進
	// FLinearColor 會整組變亮（「黑」上屏變中紫的事故，2026-07-14）。
	// DiscColor 同時是底盤與光圈遮罩的黑：兩種黑必須一模一樣才讀成同一片黑暗。
	const FLinearColor DiscColor = FLinearColor::FromSRGBColor(FColor(12, 11, 24));
	const FLinearColor WallColor = FLinearColor::FromSRGBColor(FColor(185, 179, 214));
	const FLinearColor AvatarColor = FLinearColor::FromSRGBColor(FColor(242, 237, 217));
	const FLinearColor SprayCpColor = FLinearColor::FromSRGBColor(FColor(79, 209, 181));
	const FLinearColor KickCpColor = FLinearColor::FromSRGBColor(FColor(240, 160, 74));
	const FLinearColor ExitGlowColor = FLinearColor::FromSRGBColor(FColor(217, 194, 122));
	const FLinearColor FloorGlowInner = FLinearColor::FromSRGBColor(FColor(77, 71, 112)).CopyWithNewOpacity(0.32f); // 光圈地面光（圈心）
	const FLinearColor FloorGlowOuter = FLinearColor::FromSRGBColor(FColor(77, 71, 112)).CopyWithNewOpacity(0.0f);  // 光圈地面光（圈緣）
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

	// 游標在整個死亡序列中照常活著（2026-07-13 user 定案：旋轉時人物不可移動）；
	// 人在原點＝旋轉不動點＝光圈中心，看著圈內牆型轉是抓方向感的唯一手段
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
	if (bPendingDebugPlaceExit)
	{
		bPendingDebugPlaceExit = false;
		if (Layout.ExitCell != INDEX_NONE)
		{
			PlaceAtCell(Layout.ExitCell);
		}
	}
	if (bPendingDebugPlaceRimOpposite)
	{
		bPendingDebugPlaceRimOpposite = false;
		if (Layout.ExitCell != INDEX_NONE && Layout.Sectors.Num() > 0)
		{
			const int32 Outer = Layout.Sectors.Num() - 1;
			int32 ExitRing, ExitSector;
			Layout.CellCoords(Layout.ExitCell, ExitRing, ExitSector);
			const int32 Opposite = (ExitSector + Layout.Sectors[Outer] / 2) % Layout.Sectors[Outer];
			PlaceAtCell(Layout.CellIndex(Outer, Opposite));
		}
	}

	// --- 走路（游標/視錐更新在 UpdateCursorHeading，死亡序列也活著；這裡只管移動）：
	//     按住左鍵才走路（旋轉/死亡序列中本函式不會被呼叫＝人物鎖定不可移動） ---
	const bool bForcedNav = ForcedNavRemaining > 0.0f;
	if (bForcedNav)
	{
		ForcedNavRemaining -= DeltaTime;
		CursorMazeTarget = ForcedNavTarget; // 蓋掉 UpdateCursorHeading（robo 沒有滑鼠）
	}
	APlayerController* PC = C ? Cast<APlayerController>(C->GetController()) : nullptr;
	const bool bWalkInput = PC && PC->IsInputKeyDown(EKeys::LeftMouseButton) &&
		!(C && C->IsSystemMenuOpen()); // ESC 選單開著＝點按屬於選單
	if (bWalkInput || bForcedNav)
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
	// 拳腳暫時移除（GNiceInkKickEnabled）：拾取點仍由生成器產出（種子/統計不變），
	// 但不偵測、不繪製、不授予——玩家面零痕跡
	if (GNiceInkKickEnabled && Layout.CheckpointKickCell != INDEX_NONE)
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
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	if (bExitSent)
	{
		if (Now > ExitLogCooldown)
		{
			ExitLogCooldown = Now + 1.0f;
			UE_LOG(LogTemp, Warning, TEXT("[MazeNav] TriggerExit ignored: already sent (server accepted? eyes should be open)"));
		}
		return;
	}
	// 出口只在作畫階段有效（Seating 的入座演出秒數內走不完；防禦性擋掉）
	const ANiceInkGameState* GS = GetWorld() ? GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	if (!GS || GS->CurrentPhase != ENiceInkPhase::Drawing)
	{
		if (Now > ExitLogCooldown)
		{
			ExitLogCooldown = Now + 1.0f;
			UE_LOG(LogTemp, Warning, TEXT("[MazeNav] TriggerExit blocked: phase=%d"), GS ? static_cast<int32>(GS->CurrentPhase) : -1);
		}
		return;
	}
	if (ANiceInkCharacter* C = OwnerChar())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MazeNav] TriggerExit SENT %s"), *GetDebugSummary());
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
	// 滑鼠增量推虛擬游標——滑鼠永久屬於迷宮（2026-07-15：頭/瞄準改方向鍵分軸，
	// RMB 瞄準模式退役）。死亡序列中也照常執行（旋轉中不可移動、但視錐跟游標）
	ANiceInkCharacter* C = OwnerChar();
	APlayerController* PC = C ? Cast<APlayerController>(C->GetController()) : nullptr;
	if (!PC)
	{
		return;
	}
	if (C->IsSystemMenuOpen())
	{
		return; // ESC 選單開著：滑鼠屬於選單，游標與航向凍結
	}
	{
		float MouseX = 0.0f;
		float MouseY = 0.0f;
		PC->GetInputMouseDelta(MouseX, MouseY);
		// 設計基準值 × 玩家偏好倍率（設定選單）
		const UNiceInkGameInstance* GI = UNiceInkGameInstance::Get(this);
		const float Sens = MazeCursorSensitivity * (GI ? GI->GetMouseScale() : 1.0f);
		const float Lim = FMath::Max(80.0f, LastPanelRadius * 1.15f);
		CursorPanel.X = FMath::Clamp(CursorPanel.X + MouseX * Sens, -Lim, Lim);
		CursorPanel.Y = FMath::Clamp(CursorPanel.Y - MouseY * Sens, -Lim, Lim); // 螢幕 y 向下
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

		// 出口門檻帶：在出口軌上、距出口端 < 0.18 格、游標指向門外＝直接走出。
		// 不能靠增益機制——NavMinGain(0.03) 會把末端前 1-3cm 判成「不值得走」而凍住
		//（2026-07-15 log 實錄：s=0.49/0.50 no-move，user 站在門檻上推不出去）
		for (int32 Dir = 1; Dir >= -1; Dir -= 2)
		{
			const FDreamMazeRailEndpoint& End = Dir > 0 ? Rail.EndB : Rail.EndA;
			const float Reach = Dir > 0 ? Rail.Len - RailS : RailS;
			if (End.Type == EDreamMazeRailEnd::Exit && Reach < 0.18f)
			{
				FVector2D EndPos, EndTan;
				Rail.Sample(Dir > 0 ? Rail.Len : 0.0f, EndPos, EndTan);
				const FVector2D Outward = EndTan * static_cast<float>(Dir);
				if (FVector2D::DotProduct(Target - Here, Outward) > 0.0f)
				{
					TriggerExit();
					return;
				}
			}
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
				// Exit 端點：由迴圈開頭的「出口門檻帶」先行處理（距末端 <0.18 即走出）
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
			// 沒有任何走法更接近游標＝這裡就是你指的地方的最近點。
			// 無聲失敗開口（2026-07-15 出口卡死診斷）：原地不動時每秒報告一次現場
			const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
			if (Now > StuckLogCooldown)
			{
				StuckLogCooldown = Now + 1.0f;
				UE_LOG(LogTemp, Warning, TEXT("[MazeNav] no-move D0=%.2f %s"), D0, *GetDebugSummary());
			}
			return;
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

void UDreamMazeComponent::DebugPlaceAtExitCell()
{
	bPendingDebugPlaceExit = true;
}

void UDreamMazeComponent::DebugPlaceAtRimOpposite()
{
	bPendingDebugPlaceRimOpposite = true;
}

void UDreamMazeComponent::DebugRoboNavTo(FVector2D MazeTarget, float Seconds)
{
	ForcedNavTarget = MazeTarget;
	ForcedNavRemaining = Seconds;
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
	// 軌道行走狀態（出口導航診斷）：rail=索引 s=里程/全長 endA/endB=端點型別(0=Node 1=Free 2=Exit)
	FString RailInfo = TEXT(" rail=off");
	if (bOnRail && Net.Rails.IsValidIndex(RailIdx))
	{
		const FDreamMazeRail& Rail = Net.Rails[RailIdx];
		RailInfo = FString::Printf(TEXT(" rail=%d s=%.2f/%.2f endA=%d endB=%d"),
			RailIdx, RailS, Rail.Len, static_cast<int32>(Rail.EndA.Type), static_cast<int32>(Rail.EndB.Type));
	}
	return FString::Printf(
		TEXT("active=%d state=%s pos=(%.2f,%.2f) angle=%.1f respawn=(%.2f,%.2f) cells=%d solve=%d ideal=%.1fs regen=%d traps=[%s] cpS=(%.2f,%.2f) cpK=(%.2f,%.2f) exit=(%.2f,%.2f) visitedS=%d visitedK=%d exitSent=%d killer=%d%s cur=(%.2f,%.2f)"),
		bMazeActive ? 1 : 0,
		StateNames[FMath::Clamp(static_cast<int32>(SimState), 0, 4)],
		Pos.X, Pos.Y, DisplayAngleDeg, RespawnPos.X, RespawnPos.Y,
		Layout.NumCells(), Layout.SafeSolveLen, Layout.IdealSolveSec, Layout.RegenAttempts,
		*Traps, SprayCp.X, SprayCp.Y, KickCp.X, KickCp.Y, ExitCenter.X, ExitCenter.Y,
		bSprayVisited ? 1 : 0, bKickVisited ? 1 : 0, bExitSent ? 1 : 0, LastKillerId,
		*RailInfo, CursorMazeTarget.X, CursorMazeTarget.Y);
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
	// 死亡當下人已回原點（Pos＝旋轉不動點）
	const FVector2D Eye = Pos;
	const float DiscR = Layout.RimRadius() + DreamMazeWallThickness; // 盤緣（含牆帽外緣）

	// --- 光圈式局部顯示（v3.5，2026-07-14 user 定案；射線視錐／嚴格視界／雙層光
	// r8-r15 整套退役）：圈內全亮——含牆、不做遮擋（牆不擋光）；圈外全黑。
	// 光圈半徑定義（user 原話）：「人物站在地圖原點時剛好可以看到五個等距的門」——
	// 門＝環 0 圓牆上的缺口，看到門＝看到那圈牆帶：半徑剛好蓋住環 0 牆外緣＋餘裕。
	// FogEdgeSoftness＝向外漸黑的軟邊帶寬。 ---
	const float LightR = Layout.ROuter[0] + DreamMazeWallThickness * 0.5f + 0.06f;
	const float LightFade = FMath::Clamp(P.FogEdgeSoftness, 0.05f, 1.5f);

	auto ToPx = [&](const FVector2D& MazePos) -> FVector2D
	{
		const FVector2D Rotated = MazePos.GetRotated(DisplayAngleDeg);
		return FVector2D(CenterPx.X + Rotated.X * Scale, CenterPx.Y - Rotated.Y * Scale);
	};

	// Eye 沿 Dir 到盤緣的距離（射線×圓；Eye 必在盤內＝判別式恆正）
	auto EdgeT = [&](const FVector2D& Dir) -> float
	{
		const float B = FVector2D::DotProduct(Eye, Dir);
		return -B + FMath::Sqrt(FMath::Max(0.0f, B * B - Eye.SizeSquared() + DiscR * DiscR));
	};

	// 底盤（近全黑的夢）
	Canvas->K2_DrawPolygon(nullptr, CenterPx, FVector2D(RadiusPx + 8.0f, RadiusPx + 8.0f), 48, DiscColor);

	// --- 繪製順序（光圈三步）：
	//   ①內容完整畫（光圈地面光、出口、技能點）——不做任何逐點可見性判斷
	//   ②牆完整畫（光圈附近的牆件整件：四邊形帶＋兩端圓頭）
	//   ③光圈遮罩最後裁一切：avatar 為心，LightR 內全亮、軟邊帶漸黑、之外全黑——
	//     光圈邊界是牆唯一合法的切口。 ---

	// 整件牆＝三角形帶（每段一個四邊形）＋折點/端點圓盤——全部走三角形流。
	// 血淚根因（2026-07-14 r9 紅圈診斷）：K2_DrawLine 的粗線與三角形進同一個
	// FBatchedElements 的不同陣列，GPU 端固定「先線後三角」＝提交順序失效——
	// 粗線牆永遠沉在遮罩（三角形）底下被切出平切稜角。
	// 牆一律用三角形畫＝提交順序即繪製順序；幾何算在迷宮座標（跟旋轉、跟縮放）。
	// 手動抗鋸齒（2026-07-15 user 抓到鋸齒）：canvas 批次三角形零 AA（網頁 canvas 的
	// 平滑是瀏覽器送的）——沿整條輪廓外擴 ~1.3px 的 alpha 漸層羽化裙邊＝亞像素過渡；
	// 裙邊蓋在鄰件同色實體上不可見（同色 blend＝無痕），蓋在地面上＝AA。
	const float SkirtCell = 1.3f / FMath::Max(1.0f, Scale); // 裙寬（cell）≈1.3 螢幕像素
	const FLinearColor WallEdgeColor = WallColor.CopyWithNewOpacity(0.0f);
	auto AppendWholePiece = [&](const FIntPoint& Piece, bool bCapA, bool bCapB, TArray<FCanvasUVTri>& Out)
	{
		const float HalfT = DreamMazeWallThickness * 0.5f;
		auto AddTriC = [&](const FVector2D& P0, const FLinearColor& C0,
			const FVector2D& P1, const FLinearColor& C1,
			const FVector2D& P2, const FLinearColor& C2)
		{
			FCanvasUVTri Tri;
			Tri.V0_Pos = P0;
			Tri.V1_Pos = P1;
			Tri.V2_Pos = P2;
			Tri.V0_Color = C0;
			Tri.V1_Color = C1;
			Tri.V2_Color = C2;
			Out.Add(Tri);
		};
		auto AddTri = [&](const FVector2D& P0, const FVector2D& P1, const FVector2D& P2)
		{
			AddTriC(P0, WallColor, P1, WallColor, P2, WallColor);
		};
		// 圓盤（端蓋與折點共用）：實體扇＋外圈羽化環。
		// 折點圓盤＝lineJoin='round' 等價物（凸側折點的次像素楔形縫→孤立暗點，r17 修）；
		// 端蓋圓盤＝真牆末圓潤；轉角處疊在鄰件同色實體上不可見
		auto AddDisc = [&](const FVector2D& C, int32 Sides)
		{
			const FVector2D CPx = ToPx(C);
			for (int32 k = 0; k < Sides; ++k)
			{
				const float R0 = 2.0f * PI * k / Sides;
				const float R1 = 2.0f * PI * (k + 1) / Sides;
				const FVector2D E0(FMath::Cos(R0), FMath::Sin(R0));
				const FVector2D E1(FMath::Cos(R1), FMath::Sin(R1));
				const FVector2D P0 = ToPx(C + E0 * HalfT);
				const FVector2D P1 = ToPx(C + E1 * HalfT);
				const FVector2D Q0 = ToPx(C + E0 * (HalfT + SkirtCell));
				const FVector2D Q1 = ToPx(C + E1 * (HalfT + SkirtCell));
				AddTri(CPx, P0, P1);
				AddTriC(P0, WallColor, Q0, WallEdgeColor, Q1, WallEdgeColor);
				AddTriC(P0, WallColor, Q1, WallEdgeColor, P1, WallColor);
			}
		};
		for (int32 i = Piece.X; i < Piece.Y; ++i)
		{
			const FDreamMazeOccluders::FSeg& S = Occluders.Segs[i];
			FVector2D Dir = S.B - S.A;
			const float Len = Dir.Size();
			if (Len < 1e-6f)
			{
				continue;
			}
			Dir /= Len;
			const FVector2D N(-Dir.Y * HalfT, Dir.X * HalfT);
			const FVector2D NS = N * ((HalfT + SkirtCell) / HalfT); // 法線外擴到裙緣
			const FVector2D A0 = ToPx(S.A + N);
			const FVector2D A1 = ToPx(S.A - N);
			const FVector2D B0 = ToPx(S.B + N);
			const FVector2D B1 = ToPx(S.B - N);
			AddTri(A0, A1, B1);
			AddTri(A0, B1, B0);
			// 兩側長邊羽化裙
			const FVector2D A0s = ToPx(S.A + NS);
			const FVector2D B0s = ToPx(S.B + NS);
			const FVector2D A1s = ToPx(S.A - NS);
			const FVector2D B1s = ToPx(S.B - NS);
			AddTriC(A0, WallColor, A0s, WallEdgeColor, B0s, WallEdgeColor);
			AddTriC(A0, WallColor, B0s, WallEdgeColor, B0, WallColor);
			AddTriC(A1, WallColor, A1s, WallEdgeColor, B1s, WallEdgeColor);
			AddTriC(A1, WallColor, B1s, WallEdgeColor, B1, WallColor);
			if (i > Piece.X)
			{
				AddDisc(S.A, 10); // 折點
			}
		}
		const FVector2D Ends[2] = { Occluders.Segs[Piece.X].A, Occluders.Segs[Piece.Y - 1].B };
		const bool CapFlags[2] = { bCapA, bCapB };
		for (int32 e = 0; e < 2; ++e)
		{
			if (CapFlags[e])
			{
				AddDisc(Ends[e], 16); // 端蓋
			}
		}
	};

	// ①-a 光圈地面光：圈心到圈緣的徑向漸層——亮區在無牆處也讀得出「這裡是空間」
	if (SimState != EDreamMazeSimState::Inactive)
	{
		constexpr int32 GlowSegs = 48;
		const FVector2D EyePx = ToPx(Eye);
		TArray<FCanvasUVTri> Tris;
		Tris.Reserve(GlowSegs);
		for (int32 i = 0; i < GlowSegs; ++i)
		{
			const float A0 = 2.0f * PI * i / GlowSegs;
			const float A1 = 2.0f * PI * (i + 1) / GlowSegs;
			const FVector2D D0(FMath::Cos(A0), FMath::Sin(A0));
			const FVector2D D1(FMath::Cos(A1), FMath::Sin(A1));
			// 地面光同樣鉗在盤緣（EdgeT）——站在外緣時光不得溢出外緣牆外：
			// 溢出＝把虛空畫成可走的地板＝封閉外緣看起來像出口（2026-07-15 user 兩度實測撞死的觀感謊言）
			FCanvasUVTri Tri;
			Tri.V0_Pos = EyePx;
			Tri.V1_Pos = ToPx(Eye + D0 * FMath::Min(LightR, EdgeT(D0)));
			Tri.V2_Pos = ToPx(Eye + D1 * FMath::Min(LightR, EdgeT(D1)));
			Tri.V0_Color = FloorGlowInner;
			Tri.V1_Color = Tri.V2_Color = FloorGlowOuter;
			Tris.Add(Tri);
		}
		Canvas->K2_DrawTriangle(nullptr, Tris); // nullptr＝引擎白紋理
	}

	// ①-b 技能點：先全額畫好——圈外的部分由③的遮罩塗黑，
	// 不做逐點可見性判斷（黑暗中零地標的守則由遮罩實現）
	if (SimState != EDreamMazeSimState::Inactive)
	{
		auto DrawCheckpoint = [&](int32 Cell, const FLinearColor& Color, bool bVisited, const TCHAR* Label)
		{
			if (Cell == INDEX_NONE)
			{
				return;
			}
			const FVector2D Px = ToPx(Layout.CellCenter(Cell));
			const float HalfSize = 0.24f * Scale;
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
		if (GNiceInkKickEnabled)
		{
			DrawCheckpoint(Layout.CheckpointKickCell, KickCpColor, bKickVisited, TEXT("K"));
		}

	}

	// ②牆：光圈附近的牆件整件畫（四邊形帶＋兩端圓頭，全三角形流——K2_DrawLine 粗線
	// 與三角形進不同 GPU 陣列、提交順序失效的血淚見 r9-r10 記錄）。
	// 距離粗篩＝效能規格（移植教訓）：圈外的牆反正被③全黑遮罩蓋掉，不畫＝零視覺差異
	{
		TArray<int32> NearSegs;
		Occluders.CullAround(Eye, LightR + LightFade + 0.6f, NearSegs);
		TArray<int32> SegPiece;
		SegPiece.SetNumUninitialized(Occluders.Segs.Num());
		for (int32 PieceIdx = 0; PieceIdx < Occluders.PieceRanges.Num(); ++PieceIdx)
		{
			for (int32 s = Occluders.PieceRanges[PieceIdx].X; s < Occluders.PieceRanges[PieceIdx].Y; ++s)
			{
				SegPiece[s] = PieceIdx;
			}
		}
		TSet<int32> NearPieces;
		for (const int32 SegIdx : NearSegs)
		{
			NearPieces.Add(SegPiece[SegIdx]);
		}
		TArray<FCanvasUVTri> WallTris;
		WallTris.Reserve(NearSegs.Num() * 2 + NearPieces.Num() * 32);
		for (const int32 PieceIdx : NearPieces)
		{
			AppendWholePiece(Occluders.PieceRanges[PieceIdx], true, true, WallTris);
		}
		if (WallTris.Num() > 0)
		{
			Canvas->K2_DrawTriangle(nullptr, WallTris);
		}
	}

	// ②-b 出口門柱：畫在牆之上（畫在①會被外緣牆端蓋蓋掉——第三輪截圖抓到）、
	// 遮罩之下（沒照到的門照樣沉黑＝不做地標守則不破）。
	// 門柱加大（0.1→0.22）：找到出口要一眼確定是出口（封閉外緣誤讀事故的另一半解）
	if (SimState != EDreamMazeSimState::Inactive)
	{
		const float Rim = Layout.RimRadius();
		const float ExitPhis[2] = { Layout.ExitPhi0, Layout.ExitPhi1 };
		for (const float Phi : ExitPhis)
		{
			const FVector2D Pt(Rim * FMath::Cos(Phi), Rim * FMath::Sin(Phi));
			Canvas->K2_DrawPolygon(nullptr, ToPx(Pt), FVector2D(0.22f * Scale, 0.22f * Scale), 8, ExitGlowColor);
		}
	}

	// ③光圈遮罩收尾：avatar 為心——LightR 內全亮、[LightR→LightR+Fade] 軟邊漸黑、
	// 之外全黑；楔形收在盤緣（EdgeT）防外溢。塗的黑＝DiscColor＝與底盤同一個黑。
	// 光圈邊界是牆唯一合法的切口——牆被軟邊漸黑裁切，不做角度／遮擋判斷
	if (SimState != EDreamMazeSimState::Inactive)
	{
		constexpr int32 NumWedges = 96;
		TArray<FCanvasUVTri> MaskTris;
		MaskTris.Reserve(NumWedges * 4);
		FLinearColor CIn = DiscColor;
		CIn.A = 0.0f;
		FLinearColor COut = DiscColor;
		COut.A = 1.0f;
		for (int32 i = 0; i < NumWedges; ++i)
		{
			const float A0 = 2.0f * PI * i / NumWedges;
			const float A1 = 2.0f * PI * (i + 1) / NumWedges;
			const FVector2D D0(FMath::Cos(A0), FMath::Sin(A0));
			const FVector2D D1(FMath::Cos(A1), FMath::Sin(A1));
			const float E0 = EdgeT(D0);
			const float E1 = EdgeT(D1);
			const FVector2D PIn0 = ToPx(Eye + D0 * FMath::Min(LightR, E0));
			const FVector2D PIn1 = ToPx(Eye + D1 * FMath::Min(LightR, E1));
			const FVector2D PMid0 = ToPx(Eye + D0 * FMath::Min(LightR + LightFade, E0));
			const FVector2D PMid1 = ToPx(Eye + D1 * FMath::Min(LightR + LightFade, E1));
			const FVector2D POut0 = ToPx(Eye + D0 * E0);
			const FVector2D POut1 = ToPx(Eye + D1 * E1);
			FCanvasUVTri TriA;
			TriA.V0_Pos = PIn0;
			TriA.V1_Pos = PIn1;
			TriA.V2_Pos = PMid1;
			TriA.V0_Color = TriA.V1_Color = CIn;
			TriA.V2_Color = COut;
			FCanvasUVTri TriB;
			TriB.V0_Pos = PIn0;
			TriB.V1_Pos = PMid1;
			TriB.V2_Pos = PMid0;
			TriB.V0_Color = CIn;
			TriB.V1_Color = TriB.V2_Color = COut;
			FCanvasUVTri TriC;
			TriC.V0_Pos = PMid0;
			TriC.V1_Pos = PMid1;
			TriC.V2_Pos = POut1;
			TriC.V0_Color = TriC.V1_Color = TriC.V2_Color = COut;
			FCanvasUVTri TriD;
			TriD.V0_Pos = PMid0;
			TriD.V1_Pos = POut1;
			TriD.V2_Pos = POut0;
			TriD.V0_Color = TriD.V1_Color = TriD.V2_Color = COut;
			MaskTris.Add(TriA);
			MaskTris.Add(TriB);
			MaskTris.Add(TriC);
			MaskTris.Add(TriD);
		}
		Canvas->K2_DrawTriangle(nullptr, MaskTris);
	}

	// 版本戳（排除「跑到舊 binary」的變數）——開發者遙測，只在 ni.DebugHud 1 顯示
	if (CVarNiDebugHud.GetValueOnGameThread() != 0)
	{
		FCanvasTextItem Ver(FVector2D(CenterPx.X - RadiusPx, CenterPx.Y + RadiusPx + 4.0f),
			FText::FromString(TEXT("maze-r18-aa")), GEngine->GetSmallFont(), FLinearColor(0.5f, 0.5f, 0.62f, 0.6f));
		Ver.Scale = FVector2D(0.8f, 0.8f);
		Canvas->DrawItem(Ver);
	}

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
