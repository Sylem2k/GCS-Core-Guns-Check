[BaseContainerProps(category: "STC Modules", description: "Hellgun barrel overheating. Heat builds per shot, the barrel glows through an emissive mask, smokes at high heat and locks the weapon out until it has cooled. Purely local: every machine integrates the same curve from the fire events it already receives, so there is no extra network traffic, and the tick stops itself the moment the barrel is cold.")]
class STC_HellgunOverheatModifier : ScriptedWeaponAimModifier
{
	[Attribute("0.045", UIWidgets.EditBox, "Heat added per shot, where 1.0 is a full overheat. 0.045 gives roughly 22 rounds of uninterrupted fire.")]
	protected float m_fHeatPerShot;

	[Attribute("0.14", UIWidgets.EditBox, "Heat shed per second. At 0.14 a fully cooked barrel takes about 7s to go cold.")]
	protected float m_fCoolPerSecond;

	[Attribute("0.25", UIWidgets.EditBox, "Heat at which the barrel starts to glow.")]
	protected float m_fGlowStart;

	[Attribute("0.60", UIWidgets.EditBox, "Heat at which the barrel starts to smoke.")]
	protected float m_fSmokeStart;

	[Attribute("0.50", UIWidgets.EditBox, "Heat the barrel must cool back down to before the weapon will fire again after an overheat.")]
	protected float m_fRecoverAt;

	[Attribute("60", UIWidgets.EditBox, "Emissive multiplier at maximum heat. Vanilla authored values run 0-120.")]
	protected int m_iMaxEmissive;

	[Attribute("{EB2E119AC95CF4BD}Particles/Weapon/Smoke_M60.ptc", UIWidgets.ResourceNamePicker, "Barrel smoke effect played while the barrel is over the smoke threshold.", "ptc")]
	protected ResourceName m_SmokeEffect;

	[Attribute("0 0.069 0.85", UIWidgets.Coords, "Local offset (Right Up Forward) the smoke is emitted from. Put it on the barrel, a little behind the muzzle.")]
	protected vector m_vSmokeOffset;

	// 10 Hz is enough for a heat curve and cheap enough to run on a full server.
	protected const int TICK_MS = 100;

	protected IEntity m_WeaponEnt;
	protected ParametricMaterialInstanceComponent m_Parametric;
	protected ParticleEffectEntity m_Smoke;
	protected float m_fHeat;
	protected bool m_bTicking;
	protected bool m_bOverheated;
	protected int m_iLastEmissive = -1;

	//------------------------------------------------------------------------------------------------
	override void OnInit(IEntity weaponEnt)
	{
		m_WeaponEnt = weaponEnt;
		m_Parametric = ParametricMaterialInstanceComponent.Cast(weaponEnt.FindComponent(ParametricMaterialInstanceComponent));
		m_fHeat = 0;
		m_bOverheated = false;
		ApplyGlow();
	}

	//------------------------------------------------------------------------------------------------
	override void OnWeaponFired()
	{
		m_fHeat = Math.Clamp(m_fHeat + m_fHeatPerShot, 0, 1);

		if (m_fHeat >= 1.0 && !m_bOverheated)
		{
			m_bOverheated = true;
			BlockFiring();
		}

		StartTicking();
		ApplyGlow();
		UpdateSmoke();
	}

	//------------------------------------------------------------------------------------------------
	//! Weapon was stowed or dropped - kill the smoke, but keep the heat so it keeps cooling.
	override void OnDeactivated(IEntity weaponOwner)
	{
		StopSmoke();
	}

	//------------------------------------------------------------------------------------------------
	protected void StartTicking()
	{
		if (m_bTicking)
			return;

		m_bTicking = true;
		GetGame().GetCallqueue().CallLater(Tick, TICK_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	protected void StopTicking()
	{
		if (!m_bTicking)
			return;

		m_bTicking = false;
		GetGame().GetCallqueue().Remove(Tick);
	}

	//------------------------------------------------------------------------------------------------
	protected void Tick()
	{
		m_fHeat = m_fHeat - (m_fCoolPerSecond * (TICK_MS / 1000.0));

		if (m_fHeat <= 0)
		{
			m_fHeat = 0;
			StopTicking();
		}

		if (m_bOverheated)
		{
			if (m_fHeat <= m_fRecoverAt)
				m_bOverheated = false;
			else
				BlockFiring();
		}

		ApplyGlow();
		UpdateSmoke();
	}

	//------------------------------------------------------------------------------------------------
	//! Re-applied every tick while cooked, so the weapon stays locked until the barrel recovers.
	protected void BlockFiring()
	{
		CharacterControllerComponent controller = GetCharacterController();
		if (controller)
			controller.SetWeaponNoFireTime((TICK_MS / 1000.0) * 3);
	}

	//------------------------------------------------------------------------------------------------
	protected CharacterControllerComponent GetCharacterController()
	{
		if (!m_WeaponEnt)
			return null;

		IEntity holder = m_WeaponEnt.GetRootParent();
		if (!holder || holder == m_WeaponEnt)
			return null;

		return CharacterControllerComponent.Cast(holder.FindComponent(CharacterControllerComponent));
	}

	//------------------------------------------------------------------------------------------------
	//! Drives the per-instance emissive. The emissive MASK on the material is white only over the
	//! barrel UV island, so the multiplier lights the barrel and nothing else.
	protected void ApplyGlow()
	{
		if (!m_Parametric)
			return;

		float t = 0;
		if (m_fHeat > m_fGlowStart)
			t = (m_fHeat - m_fGlowStart) / Math.Max(1.0 - m_fGlowStart, 0.001);

		t = Math.Clamp(t, 0, 1);

		int multiplier = Math.Round(t * m_iMaxEmissive);
		if (multiplier == m_iLastEmissive)
			return;

		m_iLastEmissive = multiplier;
		m_Parametric.SetEmissiveMultiplier(multiplier);

		// dull red at first, riding up to orange-white as it cooks
		int g = Math.Round(Math.Lerp(20, 190, t));
		int b = Math.Round(Math.Lerp(0, 110, t * t));
		m_Parametric.SetEmissiveColor((255 << 24) | (255 << 16) | (g << 8) | b);
	}

	//------------------------------------------------------------------------------------------------
	protected void UpdateSmoke()
	{
		bool wantSmoke = m_fHeat >= m_fSmokeStart;

		if (wantSmoke && !m_Smoke)
			SpawnSmoke();
		else if (!wantSmoke && m_Smoke)
			StopSmoke();
	}

	//------------------------------------------------------------------------------------------------
	protected void SpawnSmoke()
	{
		if (m_SmokeEffect.IsEmpty() || !m_WeaponEnt)
			return;

		ParticleEffectEntitySpawnParams params = new ParticleEffectEntitySpawnParams();
		params.Parent = m_WeaponEnt;
		params.FollowParent = m_WeaponEnt;
		params.TransformMode = ETransformMode.LOCAL;
		params.PlayOnHeadlessClient = false;
		params.DeleteWhenStopped = true;
		Math3D.MatrixIdentity4(params.Transform);
		params.Transform[3] = m_vSmokeOffset;

		m_Smoke = ParticleEffectEntity.SpawnParticleEffect(m_SmokeEffect, params);
	}

	//------------------------------------------------------------------------------------------------
	protected void StopSmoke()
	{
		if (!m_Smoke)
			return;

		m_Smoke.StopEmission();
		m_Smoke = null;
	}
}
