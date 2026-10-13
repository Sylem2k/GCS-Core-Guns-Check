#ifdef WORKBENCH
//------------------------------------------------------------------------------
// STC_PrefabDumpPlugin
// Prints prefabs AFTER inheritance is resolved, so questions like "does this object
// array merge or replace" and "how many barrels does this muzzle really have" are
// answered by the engine instead of by reading prefab text.
// Command line: -wbmodule=ResourceManager -plugin=STC_PrefabDumpPlugin
// Reads the prefab list from <addon root>/stc_dump.txt (one {GUID}path per line);
// every output line starts with "STCDUMP".
//------------------------------------------------------------------------------
[WorkbenchPluginAttribute(name: "STC Dump Resolved Prefabs", description: "Prints resolved prefab containers listed in stc_dump.txt", category: "STC", wbModules: {"ResourceManager"}, awesomeFontCode: 0xF0CB)]
class STC_PrefabDumpPlugin : WorkbenchPlugin
{
	protected const int MAX_DEPTH = 14;

	//------------------------------------------------------------------------------------------------
	override void Run()
	{
		DumpAll();
	}

	//------------------------------------------------------------------------------------------------
	override void RunCommandline()
	{
		DumpAll();
	}

	//------------------------------------------------------------------------------------------------
	protected void DumpAll()
	{
		array<string> addons = {"BFAKriegTest", "BFAGunsandammo"};
		foreach (string addon : addons)
		{
			string root;
			if (!Workbench.GetAbsolutePath("$" + addon + ":", root, false))
				continue;

			root.Replace("\\", "/");
			string listPath = root + "/stc_dump.txt";
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
				line.TrimInPlace();
				if (line.Length() == 0)
					continue;

				DumpPrefab(line);
			}
			fh.Close();
		}
		Print("STCDUMP done");
	}

	//------------------------------------------------------------------------------------------------
	protected void DumpPrefab(ResourceName prefab)
	{
		Resource res = Resource.Load(prefab);
		if (!res || !res.IsValid())
		{
			Print("STCDUMP cannot load " + prefab, LogLevel.ERROR);
			return;
		}

		string path = prefab;
		if (!path.EndsWith(".et"))
		{
			Print("STCDUMP loaded ok " + prefab);
			return;
		}

		IEntitySource src = res.GetResource().ToEntitySource();
		if (!src)
		{
			Print("STCDUMP not an entity " + prefab, LogLevel.ERROR);
			return;
		}

		Print("STCDUMP ===== " + prefab + " class " + src.GetClassName());
		DumpVars(src, "  ", 0);
		for (int i = 0, n = src.GetComponentCount(); i < n; i++)
		{
			IEntityComponentSource comp = src.GetComponent(i);
			Print("STCDUMP  component[" + i + "] " + comp.GetClassName() + " '" + comp.GetName() + "'");
			DumpVars(comp, "    ", 1);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void DumpVars(BaseContainer c, string indent, int depth)
	{
		if (!c || depth > MAX_DEPTH)
			return;

		for (int v = 0, nv = c.GetNumVars(); v < nv; v++)
		{
			string name = c.GetVarName(v);
			if (!c.IsVariableSet(name))
				continue;

			DataVarType type = c.GetDataVarType(v);
			switch (type)
			{
				case DataVarType.OBJECT:
				{
					BaseContainer obj = c.GetObject(name);
					if (obj)
					{
						Print("STCDUMP " + indent + name + " = " + obj.GetClassName() + " '" + obj.GetName() + "'");
						DumpVars(obj, indent + "  ", depth + 1);
					}
					break;
				}
				case DataVarType.OBJECT_ARRAY:
				{
					BaseContainerList list = c.GetObjectArray(name);
					int count = 0;
					if (list)
						count = list.Count();

					Print("STCDUMP " + indent + name + " [" + count + "]");
					for (int i = 0; i < count; i++)
					{
						BaseContainer item = list.Get(i);
						Print("STCDUMP " + indent + "  [" + i + "] " + item.GetClassName() + " '" + item.GetName() + "'");
						DumpVars(item, indent + "    ", depth + 1);
					}
					break;
				}
				case DataVarType.INTEGER:
				case DataVarType.FLAGS:
				{
					int iv;
					c.Get(name, iv);
					Print("STCDUMP " + indent + name + " = " + iv);
					break;
				}
				case DataVarType.BOOLEAN:
				{
					bool bv;
					c.Get(name, bv);
					Print("STCDUMP " + indent + name + " = " + bv);
					break;
				}
				case DataVarType.SCALAR:
				{
					float fv;
					c.Get(name, fv);
					Print("STCDUMP " + indent + name + " = " + fv);
					break;
				}
				case DataVarType.VECTOR3:
				{
					vector vv;
					c.Get(name, vv);
					Print("STCDUMP " + indent + name + " = " + vv);
					break;
				}
				case DataVarType.STRING:
				case DataVarType.RESOURCE_NAME:
				{
					string sv;
					c.Get(name, sv);
					Print("STCDUMP " + indent + name + " = \"" + sv + "\"");
					break;
				}
				default:
				{
					Print("STCDUMP " + indent + name + " (type " + typename.EnumToString(DataVarType, type) + ")");
					break;
				}
			}
		}
	}
}
#endif
