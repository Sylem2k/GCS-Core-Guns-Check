// 50 mm revolver mortar - ranging "on the scope glass".
// Thin subclasses of the vanilla mortar ADS displays (SCR_MortarInfo children):
//  - STC_Mortar50ElevationDisplay: replaces the degrees readout under the elevation tape with the live impact range
//    for the current tube elevation, interpolated from the round's SCR_ProjectileWindTable (1 deg firing solutions).
//  - m_bUseTurretAim (all three classes): read the elevation from the TurretComponent instead of
//    BaseWeaponManagerComponent.GetCurrentMuzzleTransform, for the case where the slot weapon's muzzle transform
//    turns out to be wrong in game (vanilla SCR_VehicleTurretElevationDisplay exists for exactly that reason).

class STC_Mortar50Aim
{
	//------------------------------------------------------------------------------------------------
	//! World aiming direction of the turret, or vector.Zero
	static vector GetTurretAimDirection(TurretControllerComponent controller)
	{
		if (!controller)
			return vector.Zero;

		TurretComponent turret = controller.GetTurretComponent();
		if (!turret)
			return vector.Zero;

		return turret.GetAimingDirectionWorld();
	}
}

//------------------------------------------------------------------------------------------------
class STC_Mortar50ElevationDisplay : SCR_MortarElevationDisplay
{
	[Attribute("{5C1A77E3B0A95605}Prefabs/Weapons/Ammo/Ammo_Mortar50_HE.et", UIWidgets.ResourcePickerThumbnail, "Round whose ProjectileWindTableConfig gives elevation -> range", "et")]
	protected ResourceName m_sRangeProjectile;

	[Attribute("%1 m", desc: "Format of the range readout (%1 = metres)")]
	protected string m_sRangeFormat;

	[Attribute("5", desc: "Round the readout to this many metres", params: "1 100")]
	protected int m_iRangeRounding;

	[Attribute("0", desc: "Use TurretComponent aiming direction instead of the weapon manager muzzle transform")]
	protected bool m_bUseTurretAim;

	protected ref SCR_ProjectileWindTable m_RangeTable;

	//------------------------------------------------------------------------------------------------
	override void DisplayStartDraw(IEntity owner)
	{
		super.DisplayStartDraw(owner);

		if (!m_RangeTable && !m_sRangeProjectile.IsEmpty())
			m_RangeTable = SCR_ProjectileWindTable.GetProjectileWindageData(m_sRangeProjectile);
	}

	//------------------------------------------------------------------------------------------------
	override void UpdateElevationInfo(vector aimRotation)
	{
		super.UpdateElevationInfo(aimRotation);

		if (!m_RangeTable || !m_wDegreesTextWidget)
			return;

		// firing solutions were generated for wind 10 m/s and init speed coef 1 (the only entry in the table)
		array<float> data = m_RangeTable.GetDataByAngleDeg(aimRotation[1], 10, 1);
		if (!data || data.Count() < 2)
		{
			m_wDegreesTextWidget.SetText("-");
			return;
		}

		int range = Math.Round(data[1] / m_iRangeRounding) * m_iRangeRounding;
		m_wDegreesTextWidget.SetTextFormat(m_sRangeFormat, range.ToString());
	}

	//------------------------------------------------------------------------------------------------
	protected override vector GetMuzzleDirection()
	{
		if (m_bUseTurretAim)
			return STC_Mortar50Aim.GetTurretAimDirection(m_TurretController);

		return super.GetMuzzleDirection();
	}
}

//------------------------------------------------------------------------------------------------
class STC_Mortar50AzimuthDisplay : SCR_MortarAzimuthDisplay
{
	[Attribute("0", desc: "Use TurretComponent aiming direction instead of the weapon manager muzzle transform")]
	protected bool m_bUseTurretAim;

	//------------------------------------------------------------------------------------------------
	protected override vector GetMuzzleDirection()
	{
		if (m_bUseTurretAim)
			return STC_Mortar50Aim.GetTurretAimDirection(m_TurretController);

		return super.GetMuzzleDirection();
	}
}

//------------------------------------------------------------------------------------------------
class STC_Mortar50BallisticTableDisplay : SCR_BallisticTableDisplay
{
	[Attribute("0", desc: "Use TurretComponent aiming direction instead of the weapon manager muzzle transform")]
	protected bool m_bUseTurretAim;

	//------------------------------------------------------------------------------------------------
	protected override vector GetMuzzleDirection()
	{
		if (m_bUseTurretAim)
			return STC_Mortar50Aim.GetTurretAimDirection(m_TurretController);

		return super.GetMuzzleDirection();
	}
}
