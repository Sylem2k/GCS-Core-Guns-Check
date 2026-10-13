//------------------------------------------------------------------------------------------------
// BFA: RG-6 revolver-cylinder ammo icon.
// Only for magazine indicator configs whose icon imageset is UI/Imagesets/BFA/BFA_RG6_Cylinder.imageset
// (Configs/WeaponInfo/MagazineIndicators/BFA_RG6_Cylinder.conf): while a cylinder is loaded, the HUD
// magazine outline (and glow) shows "rg6-cyl-<rounds>" (0..6) over the dark "rg6-cyl-bg" silhouette.
// Uses image swapping, not the progress mask: vanilla OnAmmoCountChanged computes m_fMagAmmoPerc
// as int / int, and mask levels would depend on texture colour-space handling.
// No magazine, or any other weapon: vanilla behaviour.
//------------------------------------------------------------------------------------------------
modded class SCR_WeaponInfo
{
	protected const string STC_CYLINDER_IMAGESET_PATH = "UI/Imagesets/BFA/BFA_RG6_Cylinder.imageset";
	protected const string STC_CYLINDER_IMAGE_PREFIX = "rg6-cyl-";
	protected const int STC_CYLINDER_CHAMBERS = 6;

	//------------------------------------------------------------------------------------------------
	override protected void UpdateMagazineIndicator_Textures(SCR_WeaponState state)
	{
		super.UpdateMagazineIndicator_Textures(state);
		STC_UpdateCylinderIndicator(state);
	}

	//------------------------------------------------------------------------------------------------
	override protected void UpdateMagazineIndicator_Progress(SCR_WeaponState state)
	{
		super.UpdateMagazineIndicator_Progress(state);
		STC_UpdateCylinderIndicator(state);
	}

	//------------------------------------------------------------------------------------------------
	protected void STC_UpdateCylinderIndicator(SCR_WeaponState state)
	{
		if (!state || !state.m_Magazine || !m_Widgets)
			return;

		SCR_MagazineIndicatorConfiguration config = state.m_MagazineConfig;
		if (!config)
			return;

		string icons = config.m_sImagesetIcons;
		if (!icons.Contains(STC_CYLINDER_IMAGESET_PATH))
			return;

		int rounds = state.m_iMagAmmoCount;
		if (rounds < 0)
			rounds = 0;
		if (rounds > STC_CYLINDER_CHAMBERS)
			rounds = STC_CYLINDER_CHAMBERS;

		string image = STC_CYLINDER_IMAGE_PREFIX + rounds.ToString();

		if (m_Widgets.m_wMagazineOutline)
			SetWidgetImage(m_Widgets.m_wMagazineOutline, icons, image, SIZE_MAGAZINE);

		// Glow imageset must carry the same image names; skip it if a different glow set is configured
		string glows = config.m_sImagesetGlows;
		if (m_Widgets.m_wMagazineGlow && glows.Contains(STC_CYLINDER_IMAGESET_PATH))
			SetWidgetImage(m_Widgets.m_wMagazineGlow, glows, image, SIZE_MAGAZINE);

		// m_bProgressBar 0 makes vanilla hide the background widget; show the cylinder silhouette under the icon
		if (m_Widgets.m_wMagazineBackground && !config.m_sBackground.IsEmpty())
		{
			SetWidgetImage(m_Widgets.m_wMagazineBackground, icons, config.m_sBackground, SIZE_MAGAZINE);
			m_Widgets.m_wMagazineBackground.SetVisible(true);
		}
	}
}
