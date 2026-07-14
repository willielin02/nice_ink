#pragma once

#include "CoreMinimal.h"
#include "DreamMaze.generated.h"

// 醉夢圓形迷宮（SPEC v3.3 定案 #30/#31）——難度參數。
//
// 一組＝一個難度檔；GameMode 依受害者罰酒杯數換檔（MazeParamsPerCup[0..2]，
// 「酒越深夢越深」）。所有欄位都是 playtest 旋鈕（SPEC 待定 #2），可由
// DefaultGame.ini 覆寫（config 陣列語意：ini 有任何 + 條目時先 ! 清空再全列）。
// 轉盤 5 秒／±360°／逾時 0 度是 SPEC 定案值，不進此表。
//
// 座標單位＝cell（一環厚＝1）；中心圓室半徑 1.5。設計規畫見 Docs/DREAM_MAZE_PLAN.md。
USTRUCT(BlueprintType)
struct NICEINK_API FDreamMazeParams
{
	GENERATED_BODY()

	// --- 規模／時長 ---

	// 環數（不含中心圓室）＝迷宮深度，作畫時間的主旋鈕。HUD 可讀性上限約 9。
	// 2026-07-13 操作改版（user 口述規格）：迷宮加大加複雜——7/8/9（原 5/6/7）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "2", ClampMax = "12"))
	int32 RingCount = 8;

	// 第 1 環扇形數。生成時向上取到 5 的倍數（下限 10）——K=5 等分中央門與
	// 環 1 五重對稱內殼要求扇區數整除 5（倍增 ×2 保持整除）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "4", ClampMax = "24"))
	int32 BaseSectorCount = 10;

	// 格子弧寬超過此值 ×1.5 時扇形數倍增（走廊密度／格子近似正方）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "0.6", ClampMax = "3.0"))
	float TargetCellArcWidth = 1.25f;

	// --- 拓撲質地 ---

	// 0＝長廊少岔（recursive backtracker）↔ 1＝短枝多岔（random growing-tree）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "0", ClampMax = "1"))
	float Branchiness = 0.55f;

	// 打通死路成環路的比例＝繞開陷阱與迷路自救的餘地
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "0", ClampMax = "1"))
	float BraidFactor = 0.08f;

	// 生成時挖門偏好：>0.5 徑向廊多（好定向）、<0.5 環向廊多（易暈）。
	// 改版後壓低＝正解偏環向繞行，「一路往外衝」不再成立（迷宮要抵抗）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "0", ClampMax = "1"))
	float RadialBias = 0.30f;

	// --- 視野（v3.5，2026-07-14 user 定案＝光圈式局部顯示：avatar 為心的圈內全亮
	//     ——含牆、不做遮擋；圈外全黑。射線視錐／嚴格視界／雙層光整套退役） ---

	// 【已停用 v3.5】近身光圈半徑——光圈半徑改為幾何定義（站在原點剛好看到五道
	// 等距門＝剛好蓋住環 0 門牆外緣，繪製端由 Layout 推導），不再是 per-cup 旋鈕。
	// 欄位保留（ini 相容），繪製端不再讀
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "0.5", ClampMax = "10"))
	float VisionRadius = 1.5f;

	// 光圈向外漸黑的軟邊帶寬（cell）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "0.1", ClampMax = "3"))
	float FogEdgeSoftness = 0.35f;

	// 【已停用】舊迷霧殘影秒數——視錐制下無消費者，留欄位保 ini 相容
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "0", ClampMax = "10"))
	float WallAfterglowSec = 0.0f;

	// --- 移動 ---

	// cell／秒（通關時間＋閃避反應餘裕）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "0.5", ClampMax = "8"))
	float AvatarSpeed = 2.4f;

	// --- 陷阱（數量＝其他玩家數，不在此表） ---

	// 陷阱不進前幾環（開局安全區——秒死＝怨系統）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "1", ClampMax = "6"))
	int32 TrapMinDepthRing = 2;

	// 陷阱彼此最小圖距（cell 步數）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "1", ClampMax = "8"))
	int32 TrapMinSeparation = 3;

	// 0＝均勻散佈 ↔ 1＝貼主路徑（多「擋路」）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "0", ClampMax = "1"))
	float TrapPathBias = 0.65f;

	// 【已停用 2026-07-13】陷阱破綻顯形距離——user 定案：力士陷阱在地圖上完全不可見，
	// 哪裡不能走是玩家用命記的。欄位保留（ini 相容），繪製端不再讀。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "0.5", ClampMax = "5"))
	float TellRange = 1.5f;

	// 觸發半徑（cell）。0.42≈封滿走廊（繞路是唯一解）；調小＝開放擠身而過的高風險動作
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "0.1", ClampMax = "0.6"))
	float TrapTriggerRadius = 0.42f;

	// --- 存檔點（繞路成本＝「拿技能要多睡多久」＝三杯制耦合的實體） ---

	// 繞路成本帶：d(中心→點)+d(點→出口)−d(中心→出口) 佔 d(中心→出口) 的比例
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "0", ClampMax = "2"))
	float CheckpointDetourMin = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "0", ClampMax = "3"))
	float CheckpointDetourMax = 0.55f;

	// --- 生成接受帶（退化態防護：秒通關由下界擋；永不通關靠規模上限＋braid，不加計時器）。
	// 2026-07-11 舊校準（ring5/6/7 徑向偏好）已失效——2026-07-13 改版加大環數＋壓低
	// RadialBias 後最短路顯著變長，帶值為首版估計，**要用 NiMazeStats 重開表校準**。 ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "1", ClampMax = "120"))
	float MinIdealSolveSec = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "5", ClampMax = "300"))
	float MaxIdealSolveSec = 70.0f;

	// --- 旋轉懲罰（動畫「盯得住、數不出」） ---

	// 動畫固定時長（0 度也播滿——連仁慈都不可分辨）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "1", ClampMax = "10"))
	float RotAnimDuration = 3.4f;

	// 角速度上限（度/秒）＝人眼追視極限內，定位點盯得住
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "60", ClampMax = "720"))
	float MaxAngularSpeedDeg = 240.0f;

	// 晃動振幅（度）＝度數數不出來的保證
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "0", ClampMax = "180"))
	float WobbleAmplitudeDeg = 75.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "1", ClampMax = "4"))
	int32 WobbleOctaves = 2;

	// 死亡畫面（亮兇手名——怒氣要有地址）時長；兇手轉盤等待藏在其後
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze", meta = (ClampMin = "0.3", ClampMax = "5"))
	float DeathScreenSec = 1.2f;
};

// 迷宮的邊：環邊（跨環的弧牆）或徑邊（同環相鄰扇形的徑向牆）
struct FDreamMazeEdge
{
	int32 A = INDEX_NONE;
	int32 B = INDEX_NONE;
	bool bOpen = false;
	bool bRing = false; // true＝環邊（A 在內、B 在外）
};

// 一座生成完成的迷宮（純資料，零 UObject——受害者 client 由種子決定性重建）
struct NICEINK_API FDreamMazeLayout
{
	FDreamMazeParams Params;
	int32 Seed = 0;

	int32 RingCount = 0;          // 環數（不含中心）；環索引 0..RingCount，0＝中心圓室
	TArray<int32> Sectors;        // 每環扇形數（[0]=1）
	TArray<int32> RingStart;      // 每環首格的全域索引
	TArray<float> RInner;
	TArray<float> ROuter;

	TArray<FDreamMazeEdge> Edges;
	TArray<TArray<int32>> CellEdges; // 每格接的邊索引

	int32 ExitCell = INDEX_NONE;
	float ExitPhi0 = 0.0f; // 出口缺口角域 [Phi0, Phi1)（弧度，0..2π）
	float ExitPhi1 = 0.0f;

	TArray<int32> TrapCells;               // 與兇手 PlayerId 順序對應（呼叫端配對）
	int32 CheckpointSprayCell = INDEX_NONE;
	int32 CheckpointKickCell = INDEX_NONE;

	// 生成統計（NiMazeStats／接受迴圈）
	int32 SafeSolveLen = 0;        // 中心→出口避開陷阱的最短步數
	float IdealSolveSec = 0.0f;
	float DetourRatioSpray = 0.0f;
	float DetourRatioKick = 0.0f;
	float SolveWanderRatio = 0.0f; // 正解步數÷環數：≈1＝純徑向可解＝迷宮不抵抗（記憶不值錢，旋轉偷空錢包）
	int32 SolveDecisionCount = 0;  // 正解上的岔路決策點數＝記憶難度的真正單位（環數只是粗代理）
	int32 RegenAttempts = 0;

	// --- 查詢 ---
	int32 NumCells() const { return RingStart.Num() > 0 ? RingStart.Last() + Sectors.Last() : 0; }
	int32 CellIndex(int32 Ring, int32 Sector) const { return RingStart[Ring] + Sector; }
	void CellCoords(int32 Cell, int32& OutRing, int32& OutSector) const;
	FVector2D CellCenter(int32 Cell) const;
	float RimRadius() const { return ROuter.Num() > 0 ? ROuter.Last() : 0.0f; }
	int32 CellAt(const FVector2D& Pos) const;                 // 外緣之外＝INDEX_NONE
	bool AreConnected(int32 CellA, int32 CellB) const;        // 有開邊直通
	bool IsInExitGap(float Phi) const;
	TArray<int32> OpenNeighbors(int32 Cell) const;

	// BFS（Blocked＝視為封死的格）；不可達回 -1
	int32 BFSDistance(int32 From, int32 To, const TSet<int32>& Blocked) const;
	void BFSDistanceMap(int32 From, const TSet<int32>& Blocked, TArray<int32>& OutDist) const;
	bool BFSPath(int32 From, int32 To, const TSet<int32>& Blocked, TArray<int32>& OutPath) const;

	// 子步進碰撞：牆＝閉邊；滑牆＝徑向/切向分量分別重試。
	// 撞外緣出口缺口（從出口格向外推）→ bOutExitTouched＝true、位置留在場內。
	// 元件模擬與離線 fuzz 自測共用同一份，行為零分歧。
	FVector2D ConstrainMove(const FVector2D& From, const FVector2D& Delta, bool& bOutExitTouched) const;
};

// --- 2026-07-13 操作改版：廊道中線軌道網＋視錐遮光體 ---

// 牆厚（cell）：通道寬 1−0.38＝0.62，牆厚/通道≈0.61 ≥ user 規格下限「通道一半」
constexpr float DreamMazeWallThickness = 0.38f;

enum class EDreamMazeRailEnd : uint8
{
	Node, // 格中心節點（Cell 有效）
	Free, // 接中央自由區（門口）
	Exit  // 外緣缺口——走到底＝甦醒
};

struct FDreamMazeRailEndpoint
{
	EDreamMazeRailEnd Type = EDreamMazeRailEnd::Node;
	int32 Cell = INDEX_NONE;
};

// 一條廊道中線軌（折線，迷宮座標）——「人物永遠保持在廊道中央」的載體
struct NICEINK_API FDreamMazeRail
{
	TArray<FVector2D> Pts;
	TArray<float> Cum; // 累計弧長
	float Len = 0.0f;
	FDreamMazeRailEndpoint EndA; // s=0
	FDreamMazeRailEndpoint EndB; // s=Len
	void Sample(float S, FVector2D& OutPos, FVector2D& OutTangent) const;
};

struct FDreamMazeRailRef
{
	int32 RailIdx = INDEX_NONE;
	bool bEndA = true; // 該軌以哪一端接在節點上
};

// 全迷宮軌道網：每條開邊一條軌（同環＝中線弧、跨環＝弧滑到門角＋徑向穿門）；
// 中央圓室（環 0）＝自由移動區，門口軌 EndA=Free
struct NICEINK_API FDreamMazeNet
{
	TArray<FDreamMazeRail> Rails;
	TMap<int32, TArray<FDreamMazeRailRef>> NodeRails; // cell → 接該節點的軌
	float FreeR = 0.0f; // 中央自由區半徑（扣牆厚）
	void Build(const FDreamMazeLayout& L);
};

// 視錐遮光體：所有閉牆＋外緣（出口缺口除外）的短線段集——牆擋光的幾何
struct NICEINK_API FDreamMazeOccluders
{
	struct FSeg
	{
		FVector2D A, B, Mid;
		float HalfLen = 0.0f;
	};
	TArray<FSeg> Segs;
	// 邏輯牆件分組：每件＝一堵完整的牆（一條閉邊的弧/徑向牆、或外緣一格的弧），
	// X..Y＝該件在 Segs 裡的索引範圍——「擋著的牆壁完好呈現」的物件單位
	TArray<FIntPoint> PieceRanges;
	void Build(const FDreamMazeLayout& L);
	// 每幀先以眼睛位置粗篩一次（平方距離），之後所有射線只掃這份清單——
	// 逐射線全掃 Segs 是 O(射線數×全牆數)，editor 內掉幀的元凶
	void CullAround(const FVector2D& Eye, float Range, TArray<int32>& OutIndices) const;
	// Eye 沿 Dir 首個命中距離（無命中回 MaxDist）；CulledIndices 有給就只掃它
	float Raycast(const FVector2D& Eye, const FVector2D& Dir, float MaxDist, const TArray<int32>* CulledIndices = nullptr) const;
	// Eye→Target 無遮擋？（Target 可以在牆面上：距離容差 Tol 內的命中不算擋）
	bool HasLineOfSight(const FVector2D& Eye, const FVector2D& Target, float Tol, const TArray<int32>* CulledIndices = nullptr) const;
};

// 生成器（決定性：同 Params＋Seed＋TrapCount → 同迷宮）
struct NICEINK_API FDreamMazeGen
{
	// 接受迴圈內建（理想通關時間帶、陷阱連通性）；極端參數下放寬並 log，永遠回傳可玩的迷宮
	static void Generate(const FDreamMazeParams& Params, int32 Seed, int32 TrapCount, FDreamMazeLayout& Out);

	// 每杯難度檔預設值（GameMode ctor 與離線統計共用同一份）
	static FDreamMazeParams DefaultParamsForCup(int32 Cup);

	// 離線統計：跑 NumSeeds 個種子，輸出通關時間／繞路成本／重生成率分布＋移動碰撞自測。
	// 純計算無副作用——調參前先開表（SPEC 待定 #2 的校準儀器）。
	static FString RunStats(const FDreamMazeParams& Params, int32 NumSeeds, int32 TrapCount);
};
