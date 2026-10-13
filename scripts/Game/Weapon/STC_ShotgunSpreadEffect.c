//------------------------------------------------------------------------------
// STC_ShotgunSpreadEffect
// Projectile effect for shotgun "shell" projectiles. The shell is a normal
// projectile carrying a TimerTriggerComponent; when the timer fires, this effect
// spawns m_iPelletCount pellet projectiles in a random cone around the shell's
// flight direction and launches them with the original shooter as instigator.
//
// SAFETY (2026-09-11): the shell was killing the shooter on every shot. The old
// version took the spawn point and direction straight out of `outMat`, which the
// engine documents as the HIT position/direction/normal - a timer trigger is not a
// hit, so there is nothing guaranteeing those are meaningful. If the direction came
// back degenerate the pellets were launched on an arbitrary heading from a point
// that could be right on top of the player.
//
// Now the transform is taken from the shell entity itself, every vector is checked
// before use, the spawn point is pushed clear of the muzzle, and if a sane forward
// direction cannot be established the effect spawns NOTHING rather than firing
// pellets in an unknown direction. A shell that produces no pellets is a weak shot;
// a shell that produces pellets aimed at the shooter is a dead player.
//------------------------------------------------------------------------------
class STC_ShotgunSpreadEffect : BaseProjectileEffect
{
	[Attribute("", UIWidgets.ResourceNamePicker, "Pellet projectile prefab", "et")]
	ResourceName m_sPelletPrefab;

	[Attribute("8", UIWidgets.EditBox, "Pellets spawned per shell")]
	int m_iPelletCount;

	[Attribute("1.6", UIWidgets.EditBox, "Cone half-angle in degrees")]
	float m_fSpreadAngle;

	[Attribute("0.35", UIWidgets.EditBox, "Pellets are born this far further along the shell's path [m]")]
	float m_fForwardOffset;

	[Attribute("1.2", UIWidgets.EditBox, "Never spawn a pellet closer than this to the shooter [m]")]
	float m_fMinGunnerDistance;

	//------------------------------------------------------------------------------------------------
	override void OnEffect(IEntity pHitEntity, inout vector outMat[3], IEntity damageSource, notnull Instigator instigator, string colliderName, float speed)
	{
		if (m_sPelletPrefab == "")
			return;

		Resource res = Resource.Load(m_sPelletPrefab);
		if (!res)
			return;

		IEntity gunner = instigator.GetInstigatorEntity();

		// --- where and which way is the shell actually going?
		vector pos = vector.Zero;
		vector fwd = vector.Zero;
		if (damageSource)
		{
			vector shellMat[4];
			damageSource.GetWorldTransform(shellMat);
			pos = shellMat[3];
			fwd = shellMat[2];
		}
		if (fwd.LengthSq() < 0.0001)
			fwd = outMat[1];
		if (pos.LengthSq() < 0.0001)
			pos = outMat[0];

		// No trustworthy heading means no pellets. Never guess a direction.
		if (fwd.LengthSq() < 0.0001)
			return;
		fwd = fwd.Normalized();

		// --- keep the pellets away from the man holding the gun
		pos = pos + fwd * m_fForwardOffset;
		if (gunner)
		{
			vector gunnerMat[4];
			gunner.GetWorldTransform(gunnerMat);
			float d = vector.Distance(pos, gunnerMat[3]);
			if (d < m_fMinGunnerDistance)
			{
				pos = pos + fwd * (m_fMinGunnerDistance - d);
				// still too close, or behind him: give up rather than shoot the owner
				if (vector.Distance(pos, gunnerMat[3]) < m_fMinGunnerDistance * 0.9)
					return;
			}
		}

		for (int i = 0; i < m_iPelletCount; i++)
		{
			vector dir = STC_RandomCone(fwd, m_fSpreadAngle);
			if (dir.LengthSq() < 0.0001)
				continue;

			vector tm[4];
			Math3D.DirectionAndUpMatrix(dir, vector.Up, tm);
			tm[3] = pos;

			EntitySpawnParams params = EntitySpawnParams();
			params.Transform = tm;

			IEntity pellet = GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), params);
			if (!pellet)
				continue;

			ProjectileMoveComponent move = ProjectileMoveComponent.Cast(pellet.FindComponent(ProjectileMoveComponent));
			if (!move)
				continue;

			move.Launch(dir, vector.Zero, 1.0, pellet, gunner, gunner, null, null);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Uniform random direction inside a cone of half-angle angleDeg around forward.
	protected vector STC_RandomCone(vector forward, float angleDeg)
	{
		vector ang = forward.VectorToAngles();
		float degToRad = 0.017453292519943295;
		float theta = Math.RandomFloat(0, 360) * degToRad;
		float radius = Math.Sqrt(Math.RandomFloat(0, 1)) * angleDeg;
		float yaw = ang[0] + Math.Cos(theta) * radius;
		float pitch = ang[1] + Math.Sin(theta) * radius;
		return Vector(yaw, pitch, 0).AnglesToVector().Normalized();
	}
}
