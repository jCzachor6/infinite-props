#if UNITY_IOS
using System.IO;
using UnityEditor;
using UnityEditor.Callbacks;
using UnityEditor.iOS.Xcode;

namespace InfiniteProps.Editor
{
    internal static class IosLinker
    {
        [PostProcessBuild]
        private static void Link(BuildTarget target, string path)
        {
            if (target != BuildTarget.iOS) return;
            string projectPath = PBXProject.GetPBXProjectPath(path);
            var project = new PBXProject();
            project.ReadFromFile(projectPath);
            string guid = project.GetUnityFrameworkTargetGuid();
            project.AddFrameworkToProject(guid, "libz.tbd", false);
            project.AddFrameworkToProject(guid, "libc++.tbd", false);
            project.WriteToFile(projectPath);
        }
    }
}
#endif
