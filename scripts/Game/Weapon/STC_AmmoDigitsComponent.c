[ComponentEditorProps(category: "STC Modules", description: "Ammo indicator: shifts the display panel's colour from full to empty (Stygies/CHE worn-ammo behaviour). Can optionally swap child digit models to read out the round count instead.")]
class STC_AmmoDigitsComponentClass : ScriptComponentClass
{
}

//! Put this on a weapon prefab that carries one or two child entities with a MeshObject and a
//! ParametricMaterialInstanceComponent. Every such child is tinted; with numerals enabled the child with the
//! larger local forward (z) coordinate shows the tens digit and the other the units.
//! Polled through the call queue: EOnFrame stops firing once the weapon sits in an inventory/attachment slot.
class STC_AmmoDigitsComponent : ScriptComponent
{
	[Attribute("0", UIWidgets.CheckBox, "Swap child digit models so the display reads the round count")]
	protected bool m_bShowNumerals;

	[Attribute("", UIWidgets.ResourceNamePicker, "Digit models 0-9, in order (numerals only)", "xob")]
	protected ref array<ResourceName> m_aDigitModels;

	[Attribute("1", UIWidgets.CheckBox, "Shift the display colour from full to empty")]
	protected bool m_bUseEmissive;

	[Attribute("0.25 1 0.3", UIWidgets.ColorPicker, "Panel colour with a full magazine")]
	protected vector m_vBaseFull;

	[Attribute("1 0.12 0.08", UIWidgets.ColorPicker, "Panel colour with an empty magazine")]
	protected vector m_vBaseEmpty;

	[Attribute("0.25 1 0.3", UIWidgets.ColorPicker, "Glow colour with a full magazine")]
	protected vector m_vEmissiveFull;

	[Attribute("1 0.12 0.08", UIWidgets.ColorPicker, "Glow colour with an empty magazine")]
	protected vector m_vEmissiveEmpty;

	[Attribute("25", UIWidgets.Slider, "Glow strength when empty", "0 120 1")]
	protected int m_iEmissiveMin;

	[Attribute("45", UIWidgets.Slider, "Glow strength when full", "0 120 1")]
	protected int m_iEmissiveMax;

	[Attribute("0.1", UIWidgets.EditBox, "Refresh interval in seconds")]
	protected float m_fUpdateInterval;

	protected ref array<VObject> m_aObjects = {};
	protected IEntity m_Tens;
	protected IEntity m_Units;
	protected int m_iShownCount = -1;
	protected int m_iShownColor = 0;

	override void OnPostInit(IEntity owner)
	{
		SetEventMask(owner, EntityEvent.INIT);
	}

	override void EOnInit(IEntity owner)
	{
		if (m_bShowNumerals)
		{
			if (m_aDigitModels && m_aDigitModels.Count() >= 10)
			{
				for (int i = 0; i < 10; i++)
				{
					Resource res = Resource.Load(m_aDigitModels[i]);
					VObject obj;
					if (res && res.IsValid())
						obj = res.GetResource().ToVObject();
					m_aObjects.Insert(obj);
				}
			}
			else
			{
				Print("STC_AmmoDigits: numerals enabled but fewer than 10 digit models are set", LogLevel.WARNING);
			}
		}

		FindDisplayEntities(owner);

		int interval = m_fUpdateInterval * 1000;
		if (interval < 33)
			interval = 33;
		GetGame().GetCallqueue().CallLater(Tick, interval, true);
	}

	//! Display children are the prefab's own mesh children; attachments (optics, magazines) always carry an
	//! InventoryItemComponent, so they are skipped even if they are parented later.
	protected void FindDisplayEntities(IEntity owner)
	{
		m_Tens = null;
		m_Units = null;
		float bestFwd = -1000;
		float worstFwd = 1000;
		IEntity child = owner.GetChildren();
		while (child)
		{
			if (child.FindComponent(MeshObject) && !child.FindComponent(InventoryItemComponent))
			{
				vector local = owner.CoordToLocal(child.GetOrigin());
				if (local[2] > bestFwd)
				{
					bestFwd = local[2];
					m_Tens = child;
				}
				if (local[2] < worstFwd)
				{
					worstFwd = local[2];
					m_Units = child;
				}
			}
			child = child.GetSibling();
		}
		if (m_Tens == m_Units)
			m_Tens = null;
	}

	protected void Tick()
	{
		IEntity owner = GetOwner();
		if (!owner)
		{
			GetGame().GetCallqueue().Remove(Tick);
			return;
		}
		if (!m_Units)
		{
			FindDisplayEntities(owner);
			if (!m_Units)
				return;
		}

		int count = 0;
		int maxCount = 0;
		BaseWeaponComponent weapon = BaseWeaponComponent.Cast(owner.FindComponent(BaseWeaponComponent));
		if (weapon)
		{
			BaseMagazineComponent mag = weapon.GetCurrentMagazine();
			if (mag)
			{
				count = mag.GetAmmoCount();
				maxCount = mag.GetMaxAmmoCount();
			}
		}
		count = Math.ClampInt(count, 0, 99);

		if (m_bShowNumerals && count != m_iShownCount && m_aObjects.Count() >= 10)
		{
			SetDigit(m_Units, count % 10);
			if (m_Tens)
				SetDigit(m_Tens, count / 10);
		}
		m_iShownCount = count;

		if (!m_bUseEmissive)
			return;

		float ratio = 0;
		if (maxCount > 0)
		{
			float fCount = count;
			float fMax = maxCount;
			ratio = Math.Clamp(fCount / fMax, 0, 1);
		}

		int baseArgb = PackColor(m_vBaseEmpty + (m_vBaseFull - m_vBaseEmpty) * ratio);
		if (baseArgb == m_iShownColor)
			return;

		int emArgb = PackColor(m_vEmissiveEmpty + (m_vEmissiveFull - m_vEmissiveEmpty) * ratio);
		int multiplier = m_iEmissiveMin + (m_iEmissiveMax - m_iEmissiveMin) * ratio + 0.5;
		Tint(m_Units, baseArgb, emArgb, multiplier);
		Tint(m_Tens, baseArgb, emArgb, multiplier);
		m_iShownColor = baseArgb;
	}

	protected int PackColor(vector col)
	{
		int r = Math.ClampInt(col[0] * 255 + 0.5, 0, 255);
		int g = Math.ClampInt(col[1] * 255 + 0.5, 0, 255);
		int b = Math.ClampInt(col[2] * 255 + 0.5, 0, 255);
		return (0xFF << 24) | (r << 16) | (g << 8) | b;
	}

	protected void Tint(IEntity ent, int baseArgb, int emArgb, int multiplier)
	{
		if (!ent)
			return;
		ParametricMaterialInstanceComponent pmi = ParametricMaterialInstanceComponent.Cast(ent.FindComponent(ParametricMaterialInstanceComponent));
		if (!pmi)
			return;
		pmi.SetColor(baseArgb);
		pmi.SetEmissiveColor(emArgb);
		pmi.SetEmissiveMultiplier(multiplier);
	}

	protected void SetDigit(IEntity ent, int digit)
	{
		if (!ent || digit < 0 || digit > 9)
			return;
		VObject obj = m_aObjects[digit];
		if (obj)
			ent.SetObject(obj, "");
	}
}
