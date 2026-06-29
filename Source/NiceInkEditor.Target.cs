using UnrealBuildTool;
using System.Collections.Generic;

public class NiceInkEditorTarget : TargetRules
{
	public NiceInkEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("NiceInk");
	}
}
