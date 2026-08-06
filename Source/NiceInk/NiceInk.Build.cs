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
			"VoiceChat",          // IVoiceChatUser（EOS lobby RTC 語音探針）
			"OnlineSubsystemEOS", // IOnlineSubsystemEOS::GetVoiceChatUserInterface
			"AIModule"            // 主選單舞台的 AAIController（力士編舞直驅）
		});
	}
}
