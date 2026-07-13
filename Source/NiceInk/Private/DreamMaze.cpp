#include "DreamMaze.h"

#include "Algo/Reverse.h"
#include "Math/RandomStream.h"

namespace
{
	constexpr float CenterRadius = 1.5f;   // 中心圓室半徑（cell）
	constexpr float ExitGapFraction = 0.6f; // 出口缺口佔出口格弧域比例
	constexpr float MoveSubStep = 0.1f;     // 碰撞子步長（cell）

	float WrapPhi(float Phi)
	{
		Phi = FMath::Fmod(Phi, 2.0f * PI);
		return Phi < 0.0f ? Phi + 2.0f * PI : Phi;
	}
}

// --- FDreamMazeLayout 查詢 ---

void FDreamMazeLayout::CellCoords(int32 Cell, int32& OutRing, int32& OutSector) const
{
	for (int32 Ring = RingStart.Num() - 1; Ring >= 0; --Ring)
	{
		if (Cell >= RingStart[Ring])
		{
			OutRing = Ring;
			OutSector = Cell - RingStart[Ring];
			return;
		}
	}
	OutRing = 0;
	OutSector = 0;
}

FVector2D FDreamMazeLayout::CellCenter(int32 Cell) const
{
	int32 Ring, Sector;
	CellCoords(Cell, Ring, Sector);
	if (Ring == 0)
	{
		return FVector2D::ZeroVector;
	}
	const float Rad = (RInner[Ring] + ROuter[Ring]) * 0.5f;
	const float Phi = (Sector + 0.5f) * 2.0f * PI / Sectors[Ring];
	return FVector2D(Rad * FMath::Cos(Phi), Rad * FMath::Sin(Phi));
}

int32 FDreamMazeLayout::CellAt(const FVector2D& Pos) const
{
	const float Rad = Pos.Size();
	if (Rad >= RimRadius())
	{
		return INDEX_NONE;
	}
	if (Rad < ROuter[0])
	{
		return 0;
	}
	int32 Ring = 1;
	while (Ring < ROuter.Num() && Rad >= ROuter[Ring])
	{
		++Ring;
	}
	if (Ring >= ROuter.Num())
	{
		return INDEX_NONE;
	}
	const float Phi = WrapPhi(FMath::Atan2(Pos.Y, Pos.X));
	const int32 S = Sectors[Ring];
	const int32 Sector = FMath::Clamp(FMath::FloorToInt(Phi / (2.0f * PI) * S), 0, S - 1);
	return CellIndex(Ring, Sector);
}

bool FDreamMazeLayout::AreConnected(int32 CellA, int32 CellB) const
{
	if (!CellEdges.IsValidIndex(CellA))
	{
		return false;
	}
	for (const int32 EdgeIdx : CellEdges[CellA])
	{
		const FDreamMazeEdge& Edge = Edges[EdgeIdx];
		if (Edge.bOpen && (Edge.A == CellB || Edge.B == CellB))
		{
			return true;
		}
	}
	return false;
}

bool FDreamMazeLayout::IsInExitGap(float Phi) const
{
	Phi = WrapPhi(Phi);
	return Phi >= ExitPhi0 && Phi <= ExitPhi1;
}

TArray<int32> FDreamMazeLayout::OpenNeighbors(int32 Cell) const
{
	TArray<int32> Result;
	for (const int32 EdgeIdx : CellEdges[Cell])
	{
		const FDreamMazeEdge& Edge = Edges[EdgeIdx];
		if (Edge.bOpen)
		{
			Result.Add(Edge.A == Cell ? Edge.B : Edge.A);
		}
	}
	return Result;
}

void FDreamMazeLayout::BFSDistanceMap(int32 From, const TSet<int32>& Blocked, TArray<int32>& OutDist) const
{
	OutDist.Init(-1, NumCells());
	if (!CellEdges.IsValidIndex(From) || Blocked.Contains(From))
	{
		return;
	}
	TArray<int32> Queue;
	Queue.Add(From);
	OutDist[From] = 0;
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 Cell = Queue[Head];
		for (const int32 Next : OpenNeighbors(Cell))
		{
			if (OutDist[Next] < 0 && !Blocked.Contains(Next))
			{
				OutDist[Next] = OutDist[Cell] + 1;
				Queue.Add(Next);
			}
		}
	}
}

int32 FDreamMazeLayout::BFSDistance(int32 From, int32 To, const TSet<int32>& Blocked) const
{
	TArray<int32> Dist;
	BFSDistanceMap(From, Blocked, Dist);
	return Dist.IsValidIndex(To) ? Dist[To] : -1;
}

bool FDreamMazeLayout::BFSPath(int32 From, int32 To, const TSet<int32>& Blocked, TArray<int32>& OutPath) const
{
	OutPath.Reset();
	TArray<int32> Parent;
	Parent.Init(-2, NumCells()); // -2＝未訪；-1＝根
	if (Blocked.Contains(From))
	{
		return false;
	}
	TArray<int32> Queue;
	Queue.Add(From);
	Parent[From] = -1;
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 Cell = Queue[Head];
		if (Cell == To)
		{
			break;
		}
		for (const int32 Next : OpenNeighbors(Cell))
		{
			if (Parent[Next] == -2 && !Blocked.Contains(Next))
			{
				Parent[Next] = Cell;
				Queue.Add(Next);
			}
		}
	}
	if (Parent[To] == -2)
	{
		return false;
	}
	for (int32 Cell = To; Cell != -1; Cell = Parent[Cell])
	{
		OutPath.Add(Cell);
	}
	Algo::Reverse(OutPath);
	return true;
}

FVector2D FDreamMazeLayout::ConstrainMove(const FVector2D& From, const FVector2D& Delta, bool& bOutExitTouched) const
{
	bOutExitTouched = false;
	FVector2D Pos = From;
	const float Len = Delta.Size();
	if (Len <= KINDA_SMALL_NUMBER || NumCells() == 0)
	{
		return Pos;
	}

	auto Accept = [this, &bOutExitTouched](const FVector2D& Cur, const FVector2D& Next) -> bool
	{
		const int32 CurCell = CellAt(Cur);
		const int32 NextCell = CellAt(Next);
		if (NextCell == INDEX_NONE)
		{
			// 撞外緣：只有從出口格穿過缺口才算「跨出」（延遲甦醒＝站在缺口前不動即可）
			if (CurCell == ExitCell && IsInExitGap(FMath::Atan2(Next.Y, Next.X)))
			{
				bOutExitTouched = true;
			}
			return false;
		}
		if (CurCell == INDEX_NONE)
		{
			return true; // 防禦：不知身在何處時放行回到場內
		}
		return NextCell == CurCell || AreConnected(CurCell, NextCell);
	};

	const int32 Steps = FMath::CeilToInt(Len / MoveSubStep);
	const FVector2D Sub = Delta / Steps;
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		const FVector2D Cand = Pos + Sub;
		if (Accept(Pos, Cand))
		{
			Pos = Cand;
			continue;
		}
		// 滑牆：徑向與切向分量分開重試（子步 ≤0.1 cell，不會跨兩條邊）
		FVector2D RadialDir = Pos.GetSafeNormal();
		if (RadialDir.IsNearlyZero())
		{
			RadialDir = FVector2D(1.0f, 0.0f);
		}
		const FVector2D TangentDir(-RadialDir.Y, RadialDir.X);
		const FVector2D RadialStep = RadialDir * FVector2D::DotProduct(Sub, RadialDir);
		const FVector2D TangentStep = TangentDir * FVector2D::DotProduct(Sub, TangentDir);
		if (!RadialStep.IsNearlyZero(1.e-6f) && Accept(Pos, Pos + RadialStep))
		{
			Pos += RadialStep;
		}
		if (!TangentStep.IsNearlyZero(1.e-6f) && Accept(Pos, Pos + TangentStep))
		{
			Pos += TangentStep;
		}
	}
	return Pos;
}

// --- 生成 ---

namespace
{
	void BuildGrid(const FDreamMazeParams& P, FDreamMazeLayout& L)
	{
		L.RingCount = P.RingCount;
		L.Sectors.Add(1);
		L.RingStart.Add(0);
		L.RInner.Add(0.0f);
		L.ROuter.Add(CenterRadius);

		int32 SectorCount = FMath::Max(4, P.BaseSectorCount);
		for (int32 Ring = 1; Ring <= P.RingCount; ++Ring)
		{
			const float Inner = L.ROuter.Last();
			const float Outer = Inner + 1.0f;
			if (Ring > 1)
			{
				const float MidR = (Inner + Outer) * 0.5f;
				if (2.0f * PI * MidR / SectorCount > P.TargetCellArcWidth * 1.5f)
				{
					SectorCount *= 2;
				}
			}
			L.RingStart.Add(L.RingStart.Last() + L.Sectors.Last());
			L.Sectors.Add(SectorCount);
			L.RInner.Add(Inner);
			L.ROuter.Add(Outer);
		}

		L.CellEdges.SetNum(L.NumCells());
		auto AddEdge = [&L](int32 A, int32 B, bool bRing)
		{
			const int32 Idx = L.Edges.Add({ A, B, false, bRing });
			L.CellEdges[A].Add(Idx);
			L.CellEdges[B].Add(Idx);
		};

		// 環邊（A＝內、B＝外；倍增環按外側子格切分）
		for (int32 Ring = 0; Ring < P.RingCount; ++Ring)
		{
			const int32 SIn = L.Sectors[Ring];
			const int32 SOut = L.Sectors[Ring + 1];
			for (int32 SecOut = 0; SecOut < SOut; ++SecOut)
			{
				const int32 SecIn = (Ring == 0) ? 0 : SecOut * SIn / SOut;
				AddEdge(L.CellIndex(Ring, SecIn), L.CellIndex(Ring + 1, SecOut), true);
			}
		}
		// 徑邊（A＝s、B＝s+1 mod S；渲染據此推牆的角度）
		for (int32 Ring = 1; Ring <= P.RingCount; ++Ring)
		{
			const int32 S = L.Sectors[Ring];
			if (S < 3)
			{
				continue;
			}
			for (int32 Sec = 0; Sec < S; ++Sec)
			{
				AddEdge(L.CellIndex(Ring, Sec), L.CellIndex(Ring, (Sec + 1) % S), false);
			}
		}
	}

	void CarveMaze(const FDreamMazeParams& P, FRandomStream& R, FDreamMazeLayout& L)
	{
		// growing-tree：Branchiness 在「最新格優先＝長廊」↔「隨機格＝多岔」間內插；
		// RadialBias 加權挖門方向（徑向 vs 環向）。
		TBitArray<> Visited(false, L.NumCells());
		TArray<int32> Active;
		Visited[0] = true;
		Active.Add(0);

		while (Active.Num() > 0)
		{
			const int32 Pick = (R.FRand() < 1.0f - P.Branchiness)
				? Active.Num() - 1
				: R.RandRange(0, Active.Num() - 1);
			const int32 Cell = Active[Pick];

			// 加權水塘抽樣：未訪鄰的閉邊
			int32 ChosenEdge = INDEX_NONE;
			float TotalWeight = 0.0f;
			for (const int32 EdgeIdx : L.CellEdges[Cell])
			{
				const FDreamMazeEdge& Edge = L.Edges[EdgeIdx];
				const int32 Other = (Edge.A == Cell) ? Edge.B : Edge.A;
				if (Visited[Other])
				{
					continue;
				}
				const float Weight = FMath::Max(0.02f, Edge.bRing ? P.RadialBias : 1.0f - P.RadialBias);
				TotalWeight += Weight;
				if (R.FRand() * TotalWeight <= Weight)
				{
					ChosenEdge = EdgeIdx;
				}
			}

			if (ChosenEdge == INDEX_NONE)
			{
				Active.RemoveAt(Pick);
				continue;
			}
			FDreamMazeEdge& Edge = L.Edges[ChosenEdge];
			Edge.bOpen = true;
			const int32 Other = (Edge.A == Cell) ? Edge.B : Edge.A;
			Visited[Other] = true;
			Active.Add(Other);
		}
	}

	void Braid(const FDreamMazeParams& P, FRandomStream& R, FDreamMazeLayout& L)
	{
		// 打通部分死路成環路（繞開陷阱／迷路自救的餘地）
		TArray<int32> DeadEnds;
		for (int32 Cell = 0; Cell < L.NumCells(); ++Cell)
		{
			int32 OpenDegree = 0;
			for (const int32 EdgeIdx : L.CellEdges[Cell])
			{
				OpenDegree += L.Edges[EdgeIdx].bOpen ? 1 : 0;
			}
			if (OpenDegree == 1)
			{
				DeadEnds.Add(Cell);
			}
		}
		for (int32 i = DeadEnds.Num() - 1; i > 0; --i)
		{
			DeadEnds.Swap(i, R.RandRange(0, i));
		}
		const int32 NumToBraid = FMath::RoundToInt(DeadEnds.Num() * P.BraidFactor);
		for (int32 i = 0; i < NumToBraid; ++i)
		{
			TArray<int32> ClosedEdges;
			for (const int32 EdgeIdx : L.CellEdges[DeadEnds[i]])
			{
				if (!L.Edges[EdgeIdx].bOpen)
				{
					ClosedEdges.Add(EdgeIdx);
				}
			}
			if (ClosedEdges.Num() > 0)
			{
				L.Edges[ClosedEdges[R.RandRange(0, ClosedEdges.Num() - 1)]].bOpen = true;
			}
		}
	}

	// 放出口／存檔點／陷阱＋接受檢查。RelaxLevel 隨重試遞增放寬（極端參數保底）。
	bool PlaceFeatures(const FDreamMazeParams& P, FRandomStream& R, int32 TrapCount, int32 RelaxLevel, FDreamMazeLayout& L)
	{
		const TSet<int32> NoBlock;
		const int32 NumCells = L.NumCells();

		// 出口：最外環隨機格；缺口＝該格弧域中段
		const int32 OuterRing = L.RingCount;
		const int32 ExitSector = R.RandRange(0, L.Sectors[OuterRing] - 1);
		L.ExitCell = L.CellIndex(OuterRing, ExitSector);
		const float Span = 2.0f * PI / L.Sectors[OuterRing];
		L.ExitPhi0 = ExitSector * Span + Span * (0.5f - ExitGapFraction * 0.5f);
		L.ExitPhi1 = ExitSector * Span + Span * (0.5f + ExitGapFraction * 0.5f);

		TArray<int32> DistFromCenter, DistFromExit;
		L.BFSDistanceMap(0, NoBlock, DistFromCenter);
		L.BFSDistanceMap(L.ExitCell, NoBlock, DistFromExit);
		const int32 BaseSolve = DistFromCenter[L.ExitCell];
		if (BaseSolve <= 0)
		{
			return false;
		}

		// 存檔點：繞路成本帶（拿技能要多睡多久——三杯制耦合的實體）
		float DetourMin = P.CheckpointDetourMin;
		float DetourMax = FMath::Max(P.CheckpointDetourMax, DetourMin + 0.05f);
		if (RelaxLevel >= 3)
		{
			DetourMin *= 0.3f;
			DetourMax *= 2.0f;
		}
		TArray<int32> CpCandidates;
		for (int32 Cell = 1; Cell < NumCells; ++Cell)
		{
			if (Cell == L.ExitCell || DistFromCenter[Cell] < 0)
			{
				continue;
			}
			const float Detour = float(DistFromCenter[Cell] + DistFromExit[Cell] - BaseSolve) / float(BaseSolve);
			if (Detour >= DetourMin && Detour <= DetourMax)
			{
				CpCandidates.Add(Cell);
			}
		}
		if (CpCandidates.Num() < 2)
		{
			return false;
		}
		for (int32 i = CpCandidates.Num() - 1; i > 0; --i)
		{
			CpCandidates.Swap(i, R.RandRange(0, i));
		}
		L.CheckpointSprayCell = CpCandidates[0];
		L.CheckpointKickCell = INDEX_NONE;
		for (int32 i = 1; i < CpCandidates.Num(); ++i)
		{
			if (L.BFSDistance(L.CheckpointSprayCell, CpCandidates[i], NoBlock) >= 3)
			{
				L.CheckpointKickCell = CpCandidates[i];
				break;
			}
		}
		if (L.CheckpointKickCell == INDEX_NONE)
		{
			L.CheckpointKickCell = CpCandidates[1];
		}
		auto DetourOf = [&](int32 Cell)
		{
			return float(DistFromCenter[Cell] + DistFromExit[Cell] - BaseSolve) / float(BaseSolve);
		};
		L.DetourRatioSpray = DetourOf(L.CheckpointSprayCell);
		L.DetourRatioKick = DetourOf(L.CheckpointKickCell);

		// 陷阱：貼主路加權＋深度／間隔約束＋封死後四要點連通（可歸咎鐵律：繞路必須存在）
		L.TrapCells.Reset();
		if (TrapCount > 0)
		{
			TArray<int32> MainPath;
			L.BFSPath(0, L.ExitCell, NoBlock, MainPath);
			TArray<int32> DistToPath;
			DistToPath.Init(-1, NumCells);
			{
				TArray<int32> Queue = MainPath;
				for (const int32 Cell : MainPath)
				{
					DistToPath[Cell] = 0;
				}
				for (int32 Head = 0; Head < Queue.Num(); ++Head)
				{
					for (const int32 Next : L.OpenNeighbors(Queue[Head]))
					{
						if (DistToPath[Next] < 0)
						{
							DistToPath[Next] = DistToPath[Queue[Head]] + 1;
							Queue.Add(Next);
						}
					}
				}
			}

			const int32 MinDepth = (RelaxLevel >= 4) ? 1 : P.TrapMinDepthRing;
			const int32 MinSep = (RelaxLevel >= 2) ? 1 : P.TrapMinSeparation;
			TArray<int32> Candidates;
			TArray<float> Weights;
			for (int32 Cell = 1; Cell < NumCells; ++Cell)
			{
				int32 Ring, Sector;
				L.CellCoords(Cell, Ring, Sector);
				if (Ring < MinDepth || Cell == L.ExitCell ||
					Cell == L.CheckpointSprayCell || Cell == L.CheckpointKickCell ||
					L.AreConnected(Cell, L.CheckpointSprayCell) || L.AreConnected(Cell, L.CheckpointKickCell) ||
					DistFromCenter[Cell] < 0)
				{
					continue; // 存檔點隔壁不放（存檔點＝死亡磁鐵會毀掉「自選」）
				}
				Candidates.Add(Cell);
				const float PathDist = float(FMath::Max(0, DistToPath[Cell]));
				Weights.Add(FMath::Lerp(1.0f, 1.0f / (1.0f + PathDist), P.TrapPathBias));
			}
			if (Candidates.Num() < TrapCount)
			{
				return false;
			}

			for (int32 SampleRound = 0; SampleRound < 30 && L.TrapCells.Num() < TrapCount; ++SampleRound)
			{
				TArray<int32> Chosen;
				int32 Guard = 0;
				while (Chosen.Num() < TrapCount && Guard++ < 400)
				{
					// 加權抽一格
					float Total = 0.0f;
					for (const float W : Weights)
					{
						Total += W;
					}
					float Roll = R.FRand() * Total;
					int32 PickIdx = 0;
					for (int32 i = 0; i < Weights.Num(); ++i)
					{
						Roll -= Weights[i];
						if (Roll <= 0.0f)
						{
							PickIdx = i;
							break;
						}
					}
					const int32 Cell = Candidates[PickIdx];
					bool bFarEnough = !Chosen.Contains(Cell);
					for (const int32 Placed : Chosen)
					{
						if (!bFarEnough || L.BFSDistance(Cell, Placed, NoBlock) < MinSep)
						{
							bFarEnough = false;
							break;
						}
					}
					if (bFarEnough)
					{
						Chosen.Add(Cell);
					}
				}
				if (Chosen.Num() < TrapCount)
				{
					continue;
				}
				// 連通性：陷阱格封死後 {中心, 兩存檔點, 出口} 仍互通
				const TSet<int32> TrapSet(Chosen);
				if (L.BFSDistance(0, L.ExitCell, TrapSet) >= 0 &&
					L.BFSDistance(0, L.CheckpointSprayCell, TrapSet) >= 0 &&
					L.BFSDistance(0, L.CheckpointKickCell, TrapSet) >= 0)
				{
					L.TrapCells = Chosen;
				}
			}
			if (L.TrapCells.Num() < TrapCount)
			{
				return false;
			}
		}

		// 理想通關時間帶（避開陷阱的最短路；退化態防護）
		const TSet<int32> TrapSet(L.TrapCells);
		L.SafeSolveLen = L.BFSDistance(0, L.ExitCell, TrapSet);
		L.IdealSolveSec = L.SafeSolveLen / FMath::Max(0.5f, P.AvatarSpeed);
		if (RelaxLevel < 5 && (L.IdealSolveSec < P.MinIdealSolveSec || L.IdealSolveSec > P.MaxIdealSolveSec))
		{
			return false;
		}
		return true;
	}

	bool TryGenerate(const FDreamMazeParams& P, int32 Seed, int32 TrapCount, int32 RelaxLevel, FDreamMazeLayout& Out)
	{
		Out = FDreamMazeLayout();
		Out.Params = P;
		FRandomStream R(Seed);
		BuildGrid(P, Out);
		CarveMaze(P, R, Out);
		Braid(P, R, Out);
		return PlaceFeatures(P, R, TrapCount, RelaxLevel, Out);
	}
}

void FDreamMazeGen::Generate(const FDreamMazeParams& InParams, int32 Seed, int32 TrapCount, FDreamMazeLayout& Out)
{
	FDreamMazeParams P = InParams;
	P.RingCount = FMath::Clamp(P.RingCount, 2, 12);
	P.BaseSectorCount = FMath::Clamp(P.BaseSectorCount, 4, 24);
	TrapCount = FMath::Clamp(TrapCount, 0, 8);

	for (int32 Attempt = 0; Attempt < 10; ++Attempt)
	{
		// RelaxLevel 隨重試升級：3＝放寬繞路帶、4＝放寬陷阱深度、5＝忽略時間帶
		const int32 RelaxLevel = FMath::Min(Attempt, 5);
		if (TryGenerate(P, Seed + Attempt * 7919, TrapCount, RelaxLevel, Out))
		{
			Out.Seed = Seed;
			Out.RegenAttempts = Attempt;
			return;
		}
	}
	// 全敗（極端參數）：砍陷阱數保底——寧可少陷阱也要可玩
	for (int32 Fewer = TrapCount - 1; Fewer >= 0; --Fewer)
	{
		if (TryGenerate(P, Seed + 10 * 7919, Fewer, 5, Out))
		{
			Out.Seed = Seed;
			Out.RegenAttempts = 10;
			UE_LOG(LogTemp, Warning, TEXT("DreamMaze: degenerate params, traps reduced %d->%d"), TrapCount, Fewer);
			return;
		}
	}
	Out.Seed = Seed;
	Out.RegenAttempts = 11;
	UE_LOG(LogTemp, Error, TEXT("DreamMaze: generation failed outright (seed=%d)"), Seed);
}

FDreamMazeParams FDreamMazeGen::DefaultParamsForCup(int32 Cup)
{
	// 酒越深夢越深（SPEC 待定 #2 傾向）：深檔＝更大更暗更擋路、旋轉更難追。
	// 2026-07-13 改版：整組加大（user：「現在太快就走完了」）＋壓 RadialBias 讓正解繞行；
	// 接受帶為首版估計，用 NiMazeStats 重開表後校準。
	FDreamMazeParams P; // 宣告預設＝第 1 杯檔
	switch (FMath::Clamp(Cup, 0, 2))
	{
	case 0:
		P.RingCount = 7;
		P.VisionRadius = 3.4f;
		P.Branchiness = 0.5f;
		P.BraidFactor = 0.12f;
		P.RadialBias = 0.35f;
		P.TrapPathBias = 0.5f;
		P.MinIdealSolveSec = 6.0f;
		P.MaxIdealSolveSec = 60.0f;
		P.RotAnimDuration = 3.0f;
		P.WobbleAmplitudeDeg = 60.0f;
		break;
	case 1:
		break;
	case 2:
		P.RingCount = 9;
		P.VisionRadius = 2.6f;
		P.Branchiness = 0.6f;
		P.BraidFactor = 0.05f;
		P.RadialBias = 0.25f;
		P.TrapPathBias = 0.8f;
		P.MinIdealSolveSec = 10.0f;
		P.MaxIdealSolveSec = 80.0f;
		P.RotAnimDuration = 3.8f;
		P.WobbleAmplitudeDeg = 90.0f;
		break;
	}
	return P;
}

FString FDreamMazeGen::RunStats(const FDreamMazeParams& P, int32 NumSeeds, int32 TrapCount)
{
	NumSeeds = FMath::Clamp(NumSeeds, 1, 20000);
	TArray<float> Solves, DetourS, DetourK;
	int32 RegenSum = 0;
	int32 HardFails = 0;

	for (int32 i = 0; i < NumSeeds; ++i)
	{
		FDreamMazeLayout L;
		Generate(P, 1000003 * (i + 1) + 17, TrapCount, L);
		Solves.Add(L.IdealSolveSec);
		DetourS.Add(L.DetourRatioSpray);
		DetourK.Add(L.DetourRatioKick);
		RegenSum += L.RegenAttempts;
		HardFails += (L.RegenAttempts >= 10) ? 1 : 0;
	}

	auto Pct = [](TArray<float>& Arr, float Q) -> float
	{
		if (Arr.IsEmpty())
		{
			return 0.0f;
		}
		Arr.Sort();
		const int32 Idx = FMath::Clamp(FMath::FloorToInt(Q * (Arr.Num() - 1)), 0, Arr.Num() - 1);
		return Arr[Idx];
	};

	// 移動碰撞 fuzz 自測：隨機走 4000 步，位置永遠得落在合法格內
	int32 FuzzViolations = 0;
	int32 FuzzExitTouches = 0;
	for (int32 FuzzSeed = 0; FuzzSeed < 3; ++FuzzSeed)
	{
		FDreamMazeLayout L;
		Generate(P, 424243 + FuzzSeed, TrapCount, L);
		FRandomStream R(FuzzSeed + 1);
		FVector2D Pos(0.0f, 0.0f);
		for (int32 Step = 0; Step < 4000; ++Step)
		{
			const float Angle = R.FRand() * 2.0f * PI;
			const FVector2D Delta(FMath::Cos(Angle) * 0.35f, FMath::Sin(Angle) * 0.35f);
			bool bExit = false;
			Pos = L.ConstrainMove(Pos, Delta, bExit);
			FuzzExitTouches += bExit ? 1 : 0;
			if (L.CellAt(Pos) == INDEX_NONE)
			{
				++FuzzViolations;
			}
		}
	}

	Solves.Sort(); // Printf 引數求值順序未定——先排序再取 min/max
	FString Report;
	Report += FString::Printf(TEXT("DreamMaze stats: seeds=%d traps=%d rings=%d\n"), NumSeeds, TrapCount, P.RingCount);
	Report += FString::Printf(TEXT("  ideal solve sec: p10=%.1f p50=%.1f p90=%.1f min=%.1f max=%.1f (band %.0f..%.0f)\n"),
		Pct(Solves, 0.1f), Pct(Solves, 0.5f), Pct(Solves, 0.9f), Solves[0], Solves.Last(), P.MinIdealSolveSec, P.MaxIdealSolveSec);
	Report += FString::Printf(TEXT("  spray detour ratio: p10=%.2f p50=%.2f p90=%.2f\n"),
		Pct(DetourS, 0.1f), Pct(DetourS, 0.5f), Pct(DetourS, 0.9f));
	Report += FString::Printf(TEXT("  kick detour ratio:  p10=%.2f p50=%.2f p90=%.2f\n"),
		Pct(DetourK, 0.1f), Pct(DetourK, 0.5f), Pct(DetourK, 0.9f));
	Report += FString::Printf(TEXT("  regen attempts avg=%.2f hardfails=%d\n"), float(RegenSum) / NumSeeds, HardFails);
	Report += FString::Printf(TEXT("  move fuzz: violations=%d (must be 0) exit-touches=%d\n"), FuzzViolations, FuzzExitTouches);
	return Report;
}

// --- 2026-07-13 操作改版：廊道中線軌道網＋視錐遮光體 ---

void FDreamMazeRail::Sample(float S, FVector2D& OutPos, FVector2D& OutTangent) const
{
	S = FMath::Clamp(S, 0.0f, Len);
	int32 Idx = 0;
	while (Idx < Cum.Num() - 2 && Cum[Idx + 1] < S)
	{
		++Idx;
	}
	const float SegLen = Cum[Idx + 1] - Cum[Idx];
	const float Alpha = SegLen > 1e-6f ? (S - Cum[Idx]) / SegLen : 0.0f;
	OutPos = FMath::Lerp(Pts[Idx], Pts[Idx + 1], Alpha);
	OutTangent = SegLen > 1e-6f ? (Pts[Idx + 1] - Pts[Idx]) / SegLen : FVector2D(0.0f, 1.0f);
}

namespace
{
	FVector2D MazePolar(float R, float Phi)
	{
		return FVector2D(R * FMath::Cos(Phi), R * FMath::Sin(Phi));
	}

	float MazeWrapDelta(float D)
	{
		while (D > PI) { D -= 2.0f * PI; }
		while (D < -PI) { D += 2.0f * PI; }
		return D;
	}
}

void FDreamMazeNet::Build(const FDreamMazeLayout& L)
{
	Rails.Reset();
	NodeRails.Reset();
	FreeR = L.ROuter.Num() > 0 ? L.ROuter[0] - DreamMazeWallThickness * 0.5f - 0.04f : 1.0f;

	auto RMid = [&L](int32 Ring) { return (L.RInner[Ring] + L.ROuter[Ring]) * 0.5f; };
	auto ArcPts = [](float R, float P0, float P1)
	{
		TArray<FVector2D> Pts;
		const int32 N = FMath::Max(1, FMath::CeilToInt(FMath::Abs(P1 - P0) * FMath::Max(R, 0.1f) / 0.06f));
		for (int32 i = 0; i <= N; ++i)
		{
			Pts.Add(MazePolar(R, P0 + (P1 - P0) * i / N));
		}
		return Pts;
	};
	auto LinePts = [](const FVector2D& A, const FVector2D& B)
	{
		TArray<FVector2D> Pts;
		const int32 N = FMath::Max(1, FMath::CeilToInt((B - A).Size() / 0.06f));
		for (int32 i = 0; i <= N; ++i)
		{
			Pts.Add(FMath::Lerp(A, B, float(i) / N));
		}
		return Pts;
	};
	auto MkRail = [this](TArray<FVector2D> InPts, const FDreamMazeRailEndpoint& EndA, const FDreamMazeRailEndpoint& EndB)
	{
		FDreamMazeRail Rail;
		Rail.Pts.Add(InPts[0]);
		for (int32 i = 1; i < InPts.Num(); ++i)
		{
			if ((InPts[i] - Rail.Pts.Last()).Size() > 1e-6f)
			{
				Rail.Pts.Add(InPts[i]);
			}
		}
		if (Rail.Pts.Num() < 2)
		{
			return; // 退化軌不收
		}
		Rail.Cum.Add(0.0f);
		for (int32 i = 1; i < Rail.Pts.Num(); ++i)
		{
			Rail.Cum.Add(Rail.Cum.Last() + (Rail.Pts[i] - Rail.Pts[i - 1]).Size());
		}
		Rail.Len = Rail.Cum.Last();
		Rail.EndA = EndA;
		Rail.EndB = EndB;
		const int32 Idx = Rails.Add(MoveTemp(Rail));
		if (EndA.Type == EDreamMazeRailEnd::Node) { NodeRails.FindOrAdd(EndA.Cell).Add({ Idx, true }); }
		if (EndB.Type == EDreamMazeRailEnd::Node) { NodeRails.FindOrAdd(EndB.Cell).Add({ Idx, false }); }
	};

	for (const FDreamMazeEdge& Edge : L.Edges)
	{
		if (!Edge.bOpen)
		{
			continue;
		}
		if (!Edge.bRing)
		{
			// 同環相鄰：沿環中線的弧，穿過共用牆界
			int32 Ring, SA;
			L.CellCoords(Edge.A, Ring, SA);
			const float Span = 2.0f * PI / L.Sectors[Ring];
			const float PhiA = (SA + 0.5f) * Span;
			MkRail(ArcPts(RMid(Ring), PhiA, PhiA + Span),
				{ EDreamMazeRailEnd::Node, Edge.A }, { EDreamMazeRailEnd::Node, Edge.B });
		}
		else
		{
			int32 RIn, Si, ROut, So;
			L.CellCoords(Edge.A, RIn, Si);
			L.CellCoords(Edge.B, ROut, So);
			const float SpanOut = 2.0f * PI / L.Sectors[ROut];
			const float PhiD = (So + 0.5f) * SpanOut; // 門角＝外側格中線
			if (RIn == 0)
			{
				// 中央自由區 → 環 1：門口徑向軌（EndA=Free）
				MkRail(LinePts(MazePolar(FreeR, PhiD), MazePolar(RMid(ROut), PhiD)),
					{ EDreamMazeRailEnd::Free, INDEX_NONE }, { EDreamMazeRailEnd::Node, Edge.B });
			}
			else
			{
				// 內環中線弧滑到門角，再徑向穿門到外環中線
				const float SpanIn = 2.0f * PI / L.Sectors[RIn];
				const float PhiI = (Si + 0.5f) * SpanIn;
				const float D = MazeWrapDelta(PhiD - PhiI);
				TArray<FVector2D> Pts;
				if (FMath::Abs(D) > 1e-4f)
				{
					Pts = ArcPts(RMid(RIn), PhiI, PhiI + D);
				}
				else
				{
					Pts.Add(MazePolar(RMid(RIn), PhiD));
				}
				TArray<FVector2D> Down = LinePts(MazePolar(RMid(RIn), PhiD), MazePolar(RMid(ROut), PhiD));
				for (int32 i = 1; i < Down.Num(); ++i)
				{
					Pts.Add(Down[i]);
				}
				MkRail(MoveTemp(Pts), { EDreamMazeRailEnd::Node, Edge.A }, { EDreamMazeRailEnd::Node, Edge.B });
			}
		}
	}

	// 出口軌：出口格中心 → 外緣缺口中線（走到底＝甦醒；停在門前＝延遲甦醒照舊）
	if (L.ExitCell != INDEX_NONE && L.Sectors.Num() > 0)
	{
		const int32 Outer = L.Sectors.Num() - 1;
		const float PhiE = (L.ExitPhi0 + L.ExitPhi1) * 0.5f;
		MkRail(LinePts(MazePolar(RMid(Outer), PhiE), MazePolar(L.RimRadius(), PhiE)),
			{ EDreamMazeRailEnd::Node, L.ExitCell }, { EDreamMazeRailEnd::Exit, INDEX_NONE });
	}
}

void FDreamMazeOccluders::Build(const FDreamMazeLayout& L)
{
	Segs.Reset();
	PieceRanges.Reset();
	auto AddSeg = [this](const FVector2D& A, const FVector2D& B)
	{
		Segs.Add({ A, B, (A + B) * 0.5f, static_cast<float>((B - A).Size()) * 0.5f });
	};
	auto EndPiece = [this](int32 Start)
	{
		if (Segs.Num() > Start)
		{
			PieceRanges.Add(FIntPoint(Start, Segs.Num()));
		}
	};
	auto AddArc = [&AddSeg](float R, float P0, float P1)
	{
		if (P1 - P0 < 1e-6f)
		{
			return;
		}
		const int32 N = FMath::Max(2, FMath::CeilToInt(FMath::Abs(P1 - P0) * R / 0.18f));
		FVector2D Prev = MazePolar(R, P0);
		for (int32 i = 1; i <= N; ++i)
		{
			const FVector2D P = MazePolar(R, P0 + (P1 - P0) * i / N);
			AddSeg(Prev, P);
			Prev = P;
		}
	};

	for (const FDreamMazeEdge& Edge : L.Edges)
	{
		if (Edge.bOpen)
		{
			continue;
		}
		const int32 PieceStart = Segs.Num();
		if (Edge.bRing)
		{
			int32 ROut, SOut;
			L.CellCoords(Edge.B, ROut, SOut);
			const float Span = 2.0f * PI / L.Sectors[ROut];
			AddArc(L.RInner[ROut], SOut * Span, (SOut + 1) * Span);
		}
		else
		{
			int32 Ring, SA;
			L.CellCoords(Edge.A, Ring, SA);
			const float Span = 2.0f * PI / L.Sectors[Ring];
			const float Phi = (SA + 1) * Span;
			const FVector2D PIn = MazePolar(L.RInner[Ring], Phi);
			const FVector2D POut = MazePolar(L.ROuter[Ring], Phi);
			const FVector2D PMid = (PIn + POut) * 0.5f;
			AddSeg(PIn, PMid); // 切兩段＝可見度取樣密一點
			AddSeg(PMid, POut);
		}
		EndPiece(PieceStart);
	}
	// 外緣＝遮光體（出口缺口除外）——黑暗規格下外緣不再常駐，照到才看得到
	const float Rim = L.RimRadius();
	const int32 Outer = L.Sectors.Num() - 1;
	const float SpanO = 2.0f * PI / L.Sectors[Outer];
	for (int32 S = 0; S < L.Sectors[Outer]; ++S)
	{
		const float P0 = S * SpanO;
		const float P1 = (S + 1) * SpanO;
		if (L.CellIndex(Outer, S) == L.ExitCell)
		{
			int32 PieceStart = Segs.Num();
			AddArc(Rim, P0, L.ExitPhi0);
			EndPiece(PieceStart);
			PieceStart = Segs.Num();
			AddArc(Rim, L.ExitPhi1, P1);
			EndPiece(PieceStart);
		}
		else
		{
			const int32 PieceStart = Segs.Num();
			AddArc(Rim, P0, P1);
			EndPiece(PieceStart);
		}
	}
}

void FDreamMazeOccluders::CullAround(const FVector2D& Eye, float Range, TArray<int32>& OutIndices) const
{
	OutIndices.Reset();
	for (int32 Idx = 0; Idx < Segs.Num(); ++Idx)
	{
		const FSeg& S = Segs[Idx];
		const float MaxD = Range + S.HalfLen + 0.3f;
		if ((S.Mid - Eye).SizeSquared() <= MaxD * MaxD)
		{
			OutIndices.Add(Idx);
		}
	}
}

float FDreamMazeOccluders::Raycast(const FVector2D& Eye, const FVector2D& Dir, float MaxDist, const TArray<int32>* CulledIndices) const
{
	float T = MaxDist;
	const int32 Num = CulledIndices ? CulledIndices->Num() : Segs.Num();
	for (int32 It = 0; It < Num; ++It)
	{
		const FSeg& S = Segs[CulledIndices ? (*CulledIndices)[It] : It];
		if (!CulledIndices)
		{
			const float MaxD = MaxDist + S.HalfLen + 0.3f;
			if ((S.Mid - Eye).SizeSquared() > MaxD * MaxD)
			{
				continue;
			}
		}
		const FVector2D SV = S.B - S.A;
		const float Den = Dir.X * SV.Y - Dir.Y * SV.X;
		if (FMath::Abs(Den) < 1e-9f)
		{
			continue;
		}
		const FVector2D Q = S.A - Eye;
		const float Ti = (Q.X * SV.Y - Q.Y * SV.X) / Den;
		if (Ti <= 0.0f || Ti >= T)
		{
			continue;
		}
		const float U = (Q.X * Dir.Y - Q.Y * Dir.X) / Den;
		if (U < -0.001f || U > 1.001f)
		{
			continue;
		}
		T = Ti;
	}
	return T;
}

bool FDreamMazeOccluders::HasLineOfSight(const FVector2D& Eye, const FVector2D& Target, float Tol, const TArray<int32>* CulledIndices) const
{
	const FVector2D To = Target - Eye;
	const float D = To.Size();
	if (D < 1e-4f)
	{
		return true;
	}
	const float Limit = FMath::Max(0.0f, D - Tol);
	return Raycast(Eye, To / D, Limit, CulledIndices) >= Limit - 1e-4f;
}
