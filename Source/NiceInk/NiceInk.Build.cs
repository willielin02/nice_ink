using UnrealBuildTool;

public class NiceInk : ModuleRules
{
	public NiceInk(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"NetCore",
			"CoreOnline", // FUniqueNetIdWrapper::ToString（B3 PUID 存檔鍵）
			"UMG",
			"Slate",
			"SlateCore",
			"OnlineSubsystem",
			"OnlineSubsystemUtils",
			"ProceduralMeshComponent"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"RenderCore",
			"RHI",
			"ImageWrapper",
			"ImageCore",
			"Json",               // skin_color.json 解析（自拍臉 runtime 匯入）
			"VoiceChat",          // IVoiceChatUser（EOS lobby RTC 語音探針）
			"OnlineSubsystemEOS", // IOnlineSubsystemEOS::GetVoiceChatUserInterface
			"AIModule"            // 主選單舞台的 AAIController（力士編舞直驅）
		});

		// 檔案對話框（個人檔案頁「上傳自拍」）＝Windows COM IFileOpenDialog 直呼
		// （現代對話框、DPI 清晰、Shipping 可用）——不依賴 DesktopPlatform 模組
	}
}
