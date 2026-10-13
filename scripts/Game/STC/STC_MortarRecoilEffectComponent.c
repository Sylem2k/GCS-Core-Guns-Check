// Scripts/Game/STC/STC_MortarRecoilEffectComponent.c
//
// Kicks the vehicle the mortar is mounted on every time the mortar fires.
// Lives on the MUZZLE of Mortar50_Weapon_Truck.et only, so the loose
// (ground-deployed) mortar never gets this behaviour.
//
// DEBUG BUILD: prints "STC_Recoil:" lines to the Log Console at every step.
// Remove the Print() lines once the recoil works.

[ComponentEditorProps(category: "STC/Weapons", description: "Applies a recoil impulse to the parent vehicle when the muzzle fires.")]
class STC_MortarRecoilEffectComponentClass : MuzzleEffectComponentClass
{
}

class STC_MortarRecoilEffectComponent : MuzzleEffectComponent
{
	[Attribute("1200", UIWidgets.EditBox, "Recoil impulse in N*s. Vehicle velocity change = impulse / vehicle mass. Tune by feel, not by real-world shell numbers.", params: "0 100000 1")]
	protected float m_fRecoilImpulse;

	[Attribute("0 0.35 -1.9", UIWidgets.EditBox, "Where the impulse is applied, in the VEHICLE's local space (X right, Y up, Z forward). Put it at/behind the rear axle so the rear suspension takes the hit.", params: "inf inf inf purpose=coords space=entity")]
	protected vector m_vRecoilPointLocal;

	//------------------------------------------------------------------------------------------------
	override void OnFired(IEntity effectEntity, BaseMuzzleComponent muzzle, IEntity projectileEntity)
	{
		Print(string.Format("STC_Recoil: OnFired called, isServer=%1", Replication.IsServer()));

		// Vehicle physics is server-authoritative; applying this on clients too would double the kick.
		if (!Replication.IsServer())
			return;

		// This class is an effect component, not a normal game component, so it has no GetOwner().
		// The muzzle it belongs to does, and the muzzle's owner is the weapon entity.
		IEntity weapon;
		if (muzzle)
			weapon = muzzle.GetOwner();
		if (!weapon)
			weapon = effectEntity;
		if (!weapon)
		{
			Print("STC_Recoil: no weapon entity found");
			return;
		}

		// Weapon -> turret -> vehicle. The root of the hierarchy is the truck.
		IEntity vehicle = weapon.GetRootParent();
		if (!vehicle || vehicle == weapon)
		{
			Print("STC_Recoil: weapon has no parent vehicle (root == weapon)");
			return; // not mounted on anything, nothing to kick
		}

		Physics phys = vehicle.GetPhysics();
		if (!phys)
		{
			Print("STC_Recoil: vehicle has no physics");
			return;
		}

		// Newton's third law: push opposite to where the shell went.
		// The projectile spawns aligned with the barrel, so its forward axis is the fire direction.
		vector fireDir = vector.Up;
		if (projectileEntity)
		{
			vector projMat[4];
			projectileEntity.GetWorldTransform(projMat);
			fireDir = projMat[2];
		}

		vector impulse = -fireDir * m_fRecoilImpulse;
		vector worldPoint = vehicle.CoordToParent(m_vRecoilPointLocal);

		Print(string.Format("STC_Recoil: applying impulse %1 at %2 on %3 (mass %4)", impulse, worldPoint, vehicle, phys.GetMass()));

		// A parked vehicle may have its physics asleep; wake it so the impulse isn't swallowed.
		phys.SetActive(ActiveState.ACTIVE);
		phys.ApplyImpulseAt(worldPoint, impulse);
	}
}
