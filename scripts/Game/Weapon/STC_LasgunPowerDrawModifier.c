[BaseContainerProps(category: "STC Modules", description: "Server-authoritative variable ammunition consumption for high-yield weapon variants. Add to the muzzle's WeaponAimModifiers; the engine draws 1 charge per shot natively, this consumes m_iExtraChargeCost more.")]
class STC_LasgunPowerDrawModifier : ScriptedWeaponAimModifier
{
    [Attribute("1", UIWidgets.EditBox, "Number of extra charges consumed per shot (on top of the 1 the engine deducts).")]
    protected int m_iExtraChargeCost;

    protected BaseMuzzleComponent m_Muzzle;
    protected RplComponent m_RplComponent;

    override void OnInit(IEntity weaponEnt)
    {
        WeaponComponent weaponComp = WeaponComponent.Cast(weaponEnt.FindComponent(WeaponComponent));
        if (weaponComp)
        {
            m_Muzzle = weaponComp.GetCurrentMuzzle();
        }

        // Cache the replication component once during OnInit
        m_RplComponent = RplComponent.Cast(weaponEnt.FindComponent(RplComponent));
    }

    override void OnWeaponFired()
    {
        // SetAmmoCount is master-only: bail unless we are the authority
        // (dedicated server or self-hosted client).
        if (m_RplComponent && !m_RplComponent.IsMaster())
            return;

        if (!m_Muzzle)
            return;

        BaseMagazineComponent mag = m_Muzzle.GetMagazine();
        if (!mag)
            return;

        int newCharge = mag.GetAmmoCount() - m_iExtraChargeCost;
        if (newCharge < 0)
        {
            newCharge = 0;
        }

        mag.SetAmmoCount(newCharge);
    }
}
