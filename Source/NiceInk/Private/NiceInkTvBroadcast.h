// 電視節目「夏祭り特集」影格產生器（2026-08-28 開場動畫）。
//
// user 定案：「電視機中的畫面是低解析度、像是真的舊電視的畫面，播放著日本傳統祭典影片，
// 祭典中有非常帥（甚至有點誇張、典型的帥哥刻板印象）的極道人士，讓力士們羨慕他們的刺青，
// 覺得刺上一樣的刺青就可以變一樣帥」。
//
// 構造：CPU 逐像素畫進 128×96 的 FColor 緩衝（＝真正的低解析度影像），
// 由 ANiceInkTvSet 上傳到 TF_Nearest 的 UTexture2D，再以單一 DrawTile 放大 4× 貼進
// 512×384 的 CanvasRT——**硬邊方塊像素**由此保證（畫進 512 再假裝低解析永遠會露餡）。
// 掃描線／暗角／關機白線留在 512 這一層（比影像細＝兩個解析度才像映像管）。
//
// 一切都是 (儀式拍, 拍內 alpha) 的純函式 ⇒ 各端自算、零複製欄位、遲到者自動對齊
//（與 NiceInkTvSet／入睡儀式同一條架構規則）。
#pragma once

#include "CoreMinimal.h"
#include "NiceInkTypes.h"

namespace NiceInkTvFilm
{
	// 4:3 映像管；128×96 對 RT 512×384 剛好是整數 4×（非整數倍會讓像素塊大小忽 3 忽 4＝像 bug）
	inline constexpr int32 FilmW = 128;
	inline constexpr int32 FilmH = 96;
	inline constexpr int32 FilmPx = FilmW * FilmH;

	// 分鏡編號（節目自己的鏡頭，與遊戲鏡頭無關）
	enum EShot : int32
	{
		Shot_Yomatsuri = 0, // 夜祭遠景：櫓／提燈／人群
		Shot_Taiko,         // 太鼓：兩名裸上身男子擊鼓
		Shot_Mikoshi,       // 神輿渡御：橫搖，抬轎的臂上開始出現彫物
		Shot_Reveal,        // 主役登場：半纏滑落，滿背和彫在逆光中現形
		Shot_Turn,          // 振り返り：回頭、墨鏡反光一閃、叼菸（＝「帥」的交付點）
		Shot_Num
	};

	// 儀式時間軸 → 節目分鏡。導演權在 (Step, Alpha) 上，不在牆鐘上
	//（純函式 ⇒ 各端自算、零複製欄位、遲到的客戶端自動對齊）。
	//
	// **番組本体は「寄ってから」頭出しで全部流す**（2026-08-28 user 指定
	// 「請把時間延長讓玩家可以在電視前完整看完你準備的影片片段」）：
	//   IntroSit（全景・テレビは画面上 20px 程度）＝「番組が点いている」だけを担当し、
	//   IntroNotice（電視特寫）＝夜祭→太鼓→神輿→登場→振り返りの五分鏡を頭から全部流す。
	//   観客は寄る前の内容を読めない ⇒ 寄った瞬間に頭出しでも継ぎ目は見えない。
	//
	// **OutU と OutSeconds は別の役割**（片方で兼ねると鏡長を変えた瞬間に破綻する）：
	//   OutU       ＝鏡内の**戲劇進度** 0..1（緩推・半纏の滑落・振り返り・字幕・キラッ）
	//                ⇒ 鏡を長くすれば全部ゆっくりになる＝それが正しい。
	//   OutSeconds ＝鏡頭開始からの**実秒**（太鼓の拍・提燈の揺れ・煙・火の粉）
	//                ⇒ 鏡長に依らず物理速度が保たれる。**これを U で兼ねていた一版目は、
	//                  IntroNotice を 2.6s→10.4s にした途端に太鼓が 1/4 のテンポになる。**
	void ResolveShot(ENiCeremonyStep Step, float Alpha, float StepSeconds, double Now,
		int32& OutShot, float& OutU, float& OutSeconds);

	// 畫一格。OutPixels 必須有 FilmPx 個元素（FColor，直接對應 PF_B8G8R8A8）。
	// FrameNo＝12Hz 的影格序號（噪點／抖動的亂數種子，讓每次重畫都動）。
	void RenderFrame(int32 Shot, float U, float Seconds, int32 FrameNo, FColor* OutPixels);
}
