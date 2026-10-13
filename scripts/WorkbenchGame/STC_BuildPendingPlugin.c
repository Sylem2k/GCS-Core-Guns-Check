#ifdef WORKBENCH
//------------------------------------------------------------------------------
// STC_BuildPendingPlugin
// Builds every registered source file (png / fbx / wav / txa) of the loaded STC
// addons whose runtime resource (edds / xob / ...) has not been built yet.
// A bulk "Reimport" only rebuilds resources that already have a built file, so
// freshly dropped textures with a hand-written .meta never get built by it.
// Run from Script Tools ("STC Build Pending Sources") or from the command line:
//   -wbmodule=ResourceManager -plugin=STC_BuildPendingPlugin
//------------------------------------------------------------------------------
[WorkbenchPluginAttribute(name: "STC Build Pending Sources", description: "Builds every registered png/fbx/wav/txa whose runtime resource is missing", category: "STC", wbModules: {"ResourceManager"}, awesomeFontCode: 0xF1B3)]
class STC_BuildPendingPlugin : WorkbenchPlugin
{
	protected ref array<string> m_aFound = {};
	protected bool m_bInteractive = false;

	//------------------------------------------------------------------------------------------------
	protected void OnFileFound(string fileName, FileAttribute attributes = 0, string filesystem = string.Empty)
	{
		m_aFound.Insert(fileName);
	}

	//------------------------------------------------------------------------------------------------
	override void Run()
	{
		m_bInteractive = true;
		BuildPending();
	}

	//------------------------------------------------------------------------------------------------
	override void RunCommandline()
	{
		m_bInteractive = false;
		BuildPending();
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsBuilt(ResourceManager rm, string absSource)
	{
		MetaFile meta = rm.GetMetaFile(absSource);
		if (!meta)
			return true;	// never registered: not ours to decide, treat as nothing to do

		string builtAbs;
		return Workbench.GetAbsolutePath(meta.GetResourceID().GetPath(), builtAbs, true);
	}

	//------------------------------------------------------------------------------------------------
	protected void BuildPending()
	{
		ResourceManager rm = Workbench.GetModule(ResourceManager);
		if (!rm)
		{
			Print("STC_BuildPending: ResourceManager module not available", LogLevel.ERROR);
			return;
		}

		array<string> addons = {"BFAKriegTest", "BFAGunsandammo"};
		array<string> exts = {".png", ".fbx", ".wav", ".txa"};
		array<string> pendingAbs = {};
		array<string> pendingRel = {};

		foreach (string addon : addons)
		{
			string root;
			if (!Workbench.GetAbsolutePath("$" + addon + ":", root, false))
				continue;

			root.Replace("\\", "/");
			if (root.Length() > 0 && root.Get(root.Length() - 1) == "/")
				root = root.Substring(0, root.Length() - 1);

			foreach (string ext : exts)
			{
				m_aFound.Clear();
				FileIO.FindFiles(OnFileFound, root, ext);
				foreach (string found : m_aFound)
				{
					string abs = found;
					abs.Replace("\\", "/");
					if (!FileIO.FileExists(abs))
						abs = root + "/" + abs;
					if (!FileIO.FileExists(abs))
						continue;

					MetaFile meta = rm.GetMetaFile(abs);
					if (!meta)
						continue;	// unregistered source: leave it to the user

					if (IsBuilt(rm, abs))
						continue;

					string rel = abs;
					rel.Replace(root + "/", "");
					pendingAbs.Insert(abs);
					pendingRel.Insert(rel);
				}
			}
		}

		// optional force list: <addon root>/stc_rebuild.txt, one addon-relative source path per line
		foreach (string addon2 : addons)
		{
			string root2;
			if (!Workbench.GetAbsolutePath("$" + addon2 + ":", root2, false))
				continue;
			root2.Replace("\\", "/");
			if (root2.Length() > 0 && root2.Get(root2.Length() - 1) == "/")
				root2 = root2.Substring(0, root2.Length() - 1);

			string listPath = root2 + "/stc_rebuild.txt";
			if (!FileIO.FileExists(listPath))
				continue;

			FileHandle fh = FileIO.OpenFile(listPath, FileMode.READ);
			if (!fh)
				continue;

			string line;
			while (!fh.IsEOF())
			{
				line = "";
				fh.ReadLine(line);
				line.Replace("\\", "/");
				line.TrimInPlace();
				if (line.Length() == 0)
					continue;
				if (!FileIO.FileExists(root2 + "/" + line))
				{
					Print("STC_BuildPending: force list entry not found: " + line, LogLevel.WARNING);
					continue;
				}
				Print("STC_BuildPending: force rebuild " + line, LogLevel.NORMAL);
				pendingAbs.Insert(root2 + "/" + line);
				pendingRel.Insert(line);
			}
			fh.Close();
		}

		Print(string.Format("STC_BuildPending: %1 registered source(s) without a built resource", pendingAbs.Count()), LogLevel.NORMAL);

		if (pendingRel.Count() > 0)
		{
			Print("STC_BuildPending: rebuilding registered sources", LogLevel.NORMAL);
			rm.RebuildResourceFiles(pendingRel, "PC");
		}

		if (!m_bInteractive)
		{
			Print("STC_BuildPending: rebuilds queued, the resource builder finishes them in the background", LogLevel.NORMAL);
			Print("STC_BuildPending: done", LogLevel.NORMAL);
			return;
		}

		for (int i = 0; i < pendingAbs.Count(); i++)
		{
			string abs = pendingAbs[i];
			rm.WaitForFile(abs, 15000);
			if (IsBuilt(rm, abs))
			{
				Print("STC_BuildPending: built " + pendingRel[i], LogLevel.NORMAL);
				continue;
			}

			if (!m_bInteractive)
			{
				Print("STC_BuildPending: NOT built (rebuild did nothing): " + pendingRel[i], LogLevel.WARNING);
				continue;
			}

			Print("STC_BuildPending: registering " + abs, LogLevel.NORMAL);
			rm.RegisterResourceFile(abs, true);
			rm.WaitForFile(abs, 15000);
		}

		Print("STC_BuildPending: done", LogLevel.NORMAL);
	}
}
#endif // WORKBENCH
