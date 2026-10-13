//------------------------------------------------------------------------------
// STC magazine wells — las power cell FORMATS.
// A weapon's muzzle accepts one format class; every cell prefab of that format
// carries the same class, so cells interchange across battle groups within a
// format but never across formats (and never with vanilla STANAG/9mm mags).
// Per-weapon power draw differences are handled by STC_LasgunPowerDrawModifier
// on the weapon, not by the cell.
// Weapon-unique wells (Krieg drum, Kantrael carbine) stay exclusive to their
// own weapon+cell pair because their cell is part of the weapon's model or
// carries a weapon-unique damage profile.
//------------------------------------------------------------------------------

// Root of all las power cell formats.
class STC_MagazineWellLasPack : BaseMagazineWell
{
}

// SMG-format small cell (Kantrael SMG 20rd; Las_SMG tier, 6 dmg).
class STC_MagazineWellLasPack_Small : STC_MagazineWellLasPack
{
}

// Rifle-format standard cell (LasCarbine family, Kantrael AR, Lucius AR; 13 dmg).
class STC_MagazineWellLasPack_Standard : STC_MagazineWellLasPack
{
}

// Marksman cell (Kantrael DMR, Lucius DMR, Errant/Thillium long-las, Laslock; 30 dmg).
class STC_MagazineWellLasPack_Marksman : STC_MagazineWellLasPack
{
}

// Hellgun power pack (hellgun family, 120rd; 20 dmg).
class STC_MagazineWellLasPack_Hellgun : STC_MagazineWellLasPack
{
}

// Pistol cell (laspistol, Errant hellpistol; 8 dmg).
class STC_MagazineWellLasPack_Pistol : STC_MagazineWellLasPack
{
}

// Krieg SMG drum — part of the weapon model, exclusive by design.
class STC_MagazineWellKriegDrum : BaseMagazineWell
{
}

//------------------------------------------------------------------------------
// Kantrael pattern wells. SMG/AR/DMR subclass their format so existing cell
// prefabs still fit the shared-format muzzles; the carbine stays exclusive
// (unique 7-dmg burst profile must not cross-load into other rifles).
//------------------------------------------------------------------------------
class STC_MagazineWellKantraelSMG : STC_MagazineWellLasPack_Small
{
}

class STC_MagazineWellKantraelCarbine : BaseMagazineWell
{
}

class STC_MagazineWellKantraelAR : STC_MagazineWellLasPack_Standard
{
}

class STC_MagazineWellKantraelDMR : STC_MagazineWellLasPack_Marksman
{
}

// Krieg combat shotgun shell tube. Buckshot only.
class STC_MagazineWellShotgun : BaseMagazineWell
{
}

// Lucius hellgun power pack (200rd) - exclusive to the Lucius hellgun, does not cross-load with the AR cell.
class STC_MagazineWellLasPack_LuciusHellgun : STC_MagazineWellLasPack
{
}

// Godwyn bolt pistol sickle magazine (.75 bolt). Own well so it never cross-loads with las cells.
class STC_MagazineWellBolt : BaseMagazineWell
{
}

// RG-6 six-round revolver grenade launcher cylinder (VOG-25). Own well so it never cross-loads with UGL rounds or other magazines.
class STC_MagazineWellRG6 : BaseMagazineWell
{
}

// 50 mm five-round revolver mortar cell. Own well so it never cross-loads with RG-6 or UGL rounds.
class STC_MagazineWellMortar50 : BaseMagazineWell
{
}
