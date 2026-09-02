#pragma once

#include "CoreMinimal.h"

// 打霧層的軟體光柵器（2026-09-02 灰洗分檔戰役）。
//
// **為什麼是 CPU**：分檔（淡/中/實）的均勻性要求「同檔重疊＝不變深」在**跨筆劃**
// 也成立——淡檔大面積填色必然由多條筆劃拼成，Photoshop 式的 per-stroke opacity
// 在筆劃交疊處會 +變深（0.35 疊 0.35 = 0.58）＝斑駁在玩家最需要均勻的那一檔回歸。
// 所以合成必須是**取最大**；而 UE Canvas 沒有 max 混合模式（SE_BLEND_MAX 是列舉
// 計數哨兵，實測 09-01）。CPU 逐像素合成給出精確語義：
//   新密度 ≥ 舊密度 ⇒ 整像素改寫（含顏色）。
//   ⇒ 同檔怎麼重疊都均勻（冪等）；深壓淺＝生效、淺壓深＝無事（真實墨的物理）；
//     同濃度後畫的蓋前畫的（繪畫直覺：換色重塗會生效）。
// 附帶紅利：牆＝逐像素半平面測（比多邊形裁切平滑）、羽化＝max 包絡（不再被 over
// 疊硬）、收筆淡出的低流量尾巴不會被相鄰章疊爆。
//
// **儲存語義**：每像素 FColor——RGB＝「預乘後再 sRGB 編碼」的位元組（與舊
// RTF_RGBA8 RT 的儲存逐位同語義 ⇒ 材質端零改動、上傳零轉換）、A＝密度（線性，
// 合成判準）。每一章先為（墨色×255 階密度）建 LUT ⇒ 內圈只剩比較與整像素寫入。
//
// **跨端決定性**：所有端跑同一份 IEEE 浮點碼、吃同一串 multicast 輸入 ⇒ 逐位相同
//（與 PointWall「各端自算必同」同一根據）。
class FInkMistSurface
{
public:
	struct FPlane
	{
		FVector2D NormalUv = FVector2D(1.0, 0.0); // 由章心指向牆的單位法線
		float DistUv = 0.0f;                      // 保留 dot(p−章心,N) <= Dist 的一側
	};

	void Init(int32 InRes);
	bool IsInited() const { return Res > 0; }
	int32 Resolution() const { return Res; }
	void Clear();

	// 圓章（硬心＋smoothstep 軟邊；與退役的 MistDiscTexture 同剖面式）。
	// Color＝線性墨色（未預乘）；Density＝Tier×Flow×AlphaCenter（0..1）。
	void StampDisc(const FVector2D& CenterUV, float RadiusUv, float FeatherFrac,
		const FLinearColor& Color, float Density, const FPlane* Planes, int32 NumPlanes);

	// 單點（縫區逐點路徑／舊制折線段模擬）：純圓、線性斜坡邊（寬 EdgeUv）。
	// Density 直接＝該點目標濃度——max 合成下重疊點取最大＝剖面本身
	//（舊制 over 疊合的 1−(1−t)^(1/N) 反解隨之退役）。
	void StampDot(const FVector2D& CenterUV, float RadiusUv, float EdgeUv,
		const FLinearColor& Color, float Density);

	// 自上次 Take 以來的髒區；無髒區回 false。
	bool TakeDirty(FIntRect& OutRect);
	// 全區標髒（Clear／重播完成後整張上傳用）
	void MarkAllDirty();

	// 罩染的筆劃邊界（09-02 三修）：罩染只發生在**跨筆劃**——同一筆的淡出尾巴
	// 掃過自己的頭不算罩染（比值閘擋不住連續漸變：線性淡出在低流量端相鄰章的
	// 相對落差自然超過任何固定比值）。每開一筆呼叫一次；重放端每筆同呼＝同構。
	void BeginStrokeMark();

	// 顯示位元組（09-02 罩染制）：無罩染＝直接是基底緩衝（零複製）；有罩染＝
	// 呼叫端用 ComposeInto 把「罩染 over 基底」預合成到暫存再上傳。
	const FColor* Data() const { return Pixels.GetData(); }
	bool HasGlaze() const { return Glaze.Num() > 0; }
	// 把矩形內的「罩染 over 基底」合成寫進 Out（rect-local、OutPitchPx＝Out 的列寬）。
	// 合成在線性域做（sRGB 解碼→premult over→再編碼）；無罩染像素＝直拷基底。
	void ComposeInto(FColor* Out, int32 OutPitchPx, const FIntRect& Rect) const;

	// --- 墨的光學（09-02）：密度→外觀的**唯一**函數，烘在 LUT 裡 ---
	// 材質端做的是 premult over（mist.rgb + skin×(1−a)）、我們完全控制 rgb ⇒
	// 「薄墨偏冷、疊滿回筆色」不需要動材質圖——07-24 冷墨底色想做而做錯位置的事
	//（常數地板＝疊滿也藍；離線優化證明常數解不存在），做成濃度的函數就自洽了。
	// 彩墨由色度閘豁免（稀的紅＝粉紅，不是藍——Tyndall 是黑墨的物理）。
	// 皮膚錨定紋理：乘在密度上的畫布座標噪場——**畫布＝UV0＝皮膚** ⇒ 錨在這裡
	// 就是錨在皮膚（顆粒錨皮膚＝讀成質感、錨筆劃＝讀成髒——五輪雲斑的教訓）；
	// 純像素座標函數 ⇒ 跨筆劃/跨端/重建恆同、max 冪等不破。
	void Configure(const FLinearColor& InToneCool, float InToneStrength, float InToneGamma, float InGrainAmp, float InGrainPeriodPx);

private:
	int32 Res = 0;
	// 基底槽：max 合成（新密度 ≥ 舊＝整像素改寫）——均勻性的構造保證
	TArray64<FColor> Pixels;
	// 罩染槽（09-02；user 以真實刺青流程定案「薄墨必須罩得上任何底」）：
	// 新墨比基底**淡**＝進這裡（自己也 max＝罩染亂掃照樣均勻）；顯示＝罩染 over
	// 基底。基底被更濃的墨改寫時該像素罩染清空（新的實墨＝新的表面）。
	// 惰性配置：第一次罩染才配——大多數畫布永遠不罩染＝零成本。
	TArray64<FColor> Glaze;
	// 逐像素「基底最後由哪一筆寫」（與 Glaze 同時惰性配置；uint16 世代）：
	// 罩染條件之一＝基底不是本筆寫的（0＝配置前的舊墨＝視為別筆 ✓）
	TArray64<uint16> BaseEpoch;
	uint16 CurrentEpoch = 1;
	FIntRect Dirty;
	bool bDirty = false;

	void EnsureGlaze();

	// 墨光學參數（Configure 設；變更＝LUT 失效）
	FLinearColor ToneCool = FLinearColor(0.040f, 0.055f, 0.100f);
	float ToneStrength = 0.0f; // 0＝所有色同色相只差透明度（09-02 user 指令）
	float ToneGamma = 1.0f;
	float GrainAmp = 0.05f;
	float GrainPeriodPx = 4.4f;

	// 每章 LUT：密度位元組 → 最終像素（色相曲線＋預乘＋sRGB 編碼一次算完）
	FColor Lut[256];
	FLinearColor LutColor = FLinearColor(-1.0f, -1.0f, -1.0f, -1.0f);

	void BuildLut(const FLinearColor& Color);
	float GrainAt(int32 X, int32 Y) const;
	void ExpandDirty(int32 X0, int32 Y0, int32 X1, int32 Y1);
};
