//------------------------------------------------------------------------------
// STC_AirburstFuzeComponent
// Height-of-burst fuze for magazine-fed mortar bombs (50 mm revolver mortar,
// "Shrapnel Airburst" round). Goes on the PROJECTILE prefab next to its normal
// CollisionTriggerComponent. On the descending branch it measures the height above
// whatever is under the bomb (terrain, sea, roofs, tree crowns, vehicles) and the
// clear distance along the flight path, and when either drops to m_fBurstHeight it
// forces the projectile's own trigger with BaseTriggerComponent.OnUserTrigger().
// The trigger then runs its PROJECTILE_EFFECTS exactly as on impact (warhead,
// particle, sound). If nothing makes it burst, the impact fuze still works.
//
// WHY A SCRIPT: the only native non-impact triggers are TimerTriggerComponent
// (fixed time, replaces the impact trigger in every vanilla use) and
// ProximityTriggerComponent (one DISTANCE property, entity query, no vanilla user).
// Neither gives a constant height of burst from 100 m to 700 m without a fuze-
// setting UI, which a magazine-fed round does not have.
//
// WHO RUNS IT: every machine. The round is a non-replicated projectile (its
// RplComponent is disabled, like every vanilla magazine grenade round), so the
// server and each client fly their own copy and each copy's CollisionTrigger
// already detonates locally on impact. This component does the same for the
// airburst. Vanilla forces triggers the same way on all machines
// (SCR_PressureTriggerComponent: CallLater(RPC_DoTrigger) + Rpc(RPC_DoTrigger)
// with RplRcver.Broadcast -> OnUserTriggerOverrideInstigator on every peer), so
// damage authority is left to the engine, as for the impact fuze.
//
// HOW OFTEN: through the script call queue, not EOnFrame (a projectile is driven
// by ProjectileSimulationSystem; STC_AmmoDigitsComponent already found EOnFrame
// unreliable outside the plain world). Idle cartridges poll at 4 Hz; in flight the
// wait shrinks as the bomb nears the ground; the last m_fCloseHeight metres are
// checked every frame with half a frame of look-ahead, so the burst lands within
// about +-half a frame's drop of the set height (+-2 m at 30 fps, 120 m/s).
//------------------------------------------------------------------------------
[ComponentEditorProps(category: "STC Modules", description: "Height-of-burst fuze: forces the projectile's own trigger when it comes within a set height of the surface under it (or of anything ahead of it) on the descending branch. The impact trigger stays as the fallback.")]
class STC_AirburstFuzeComponentClass : ScriptComponentClass
{
	//------------------------------------------------------------------------------------------------
	//! Needs the trigger it fires
	static override array<typename> Requires(IEntityComponentSource src)
	{
		array<typename> requires = {};

		requires.Insert(BaseTriggerComponent);

		return requires;
	}
}

class STC_AirburstFuzeComponent : ScriptComponent
{
	[Attribute("7.5", UIWidgets.EditBox, "Burst height above the surface under the bomb [m]. Low for a shrapnel airburst, high (about 200) for the illumination round's flare deploy. Keep it BELOW m_fCloseHeight or the fuze never tests it.", "1 400")]
	protected float m_fBurstHeight;

	[Attribute("45", UIWidgets.EditBox, "The fuze arms only this far (3D) from the launch point [m]. Keep it above the trigger's SafetyDistance.", "0 1000")]
	protected float m_fArmDistance;

	[Attribute("1.5", UIWidgets.EditBox, "The fuze arms only after this flight time [s]", "0 60")]
	protected float m_fArmTime;

	[Attribute("40", UIWidgets.EditBox, "Below this height the fuze checks every frame [m]", "5 500")]
	protected float m_fCloseHeight;

	[Attribute("30", UIWidgets.EditBox, "Projectile speed that counts as fired [m/s]. Keeps a still or dropped cartridge from arming.", "1 1000")]
	protected float m_fLaunchSpeed;

	[Attribute("1", UIWidgets.CheckBox, "Trace for roofs, trees and vehicles under and ahead of the bomb. Off = terrain and sea only.")]
	protected bool m_bTraceObjects;

	[Attribute("0", UIWidgets.CheckBox, "Print every burst to the console (testing)")]
	protected bool m_bDebug;

	protected static const int IDLE_POLL_MS = 250;
	protected static const int MAX_POLL_MS = 1000;
	protected static const float GRAVITY = 9.81;
	protected static const float MAX_TRACE_DEPTH = 200;	// no downward trace when higher than this above the terrain [m]
	protected static const float DEFAULT_FRAME = 0.033;	// frame time used before a real one is measured [s]

	protected BaseTriggerComponent m_Trigger;
	protected ProjectileMoveComponent m_Move;
	protected ref TraceParam m_Trace;

	protected bool m_bLaunched;
	protected bool m_bDone;
	protected vector m_vLaunchPos;
	protected float m_fLaunchTime;	// world time [ms]
	protected float m_fLastTick;	// world time [ms]
	protected int m_iRetries;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		if (SCR_Global.IsEditMode(owner))
			return;

		m_Trigger = BaseTriggerComponent.Cast(owner.FindComponent(BaseTriggerComponent));
		m_Move = ProjectileMoveComponent.Cast(owner.FindComponent(ProjectileMoveComponent));
		if (!m_Trigger || !m_Move)
		{
			Print("STC_AirburstFuzeComponent: needs a BaseTriggerComponent and a ProjectileMoveComponent on the same projectile - fuze disabled", LogLevel.ERROR);
			return;
		}

		m_Trace = new TraceParam();
		Schedule(IDLE_POLL_MS);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		ArmaReforgerScripted game = GetGame();
		if (game && game.GetCallqueue())
		{
			game.GetCallqueue().Remove(Tick);
			game.GetCallqueue().Remove(VerifyBurst);
		}

		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	protected void Schedule(float delayMs)
	{
		if (m_bDone)
			return;

		if (delayMs <= 0)
			GetGame().GetCallqueue().Call(Tick);	// next frame
		else
			GetGame().GetCallqueue().CallLater(Tick, delayMs, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void Tick()
	{
		if (m_bDone)
			return;

		IEntity owner = GetOwner();
		if (!owner || !m_Trigger || !m_Move)
			return;

		if (m_Trigger.WasTriggered())
		{
			m_bDone = true;	// the impact fuze got there first
			return;
		}

		BaseWorld world = owner.GetWorld();
		if (!world)
			return;

		float now = world.GetWorldTime();
		float dt = DEFAULT_FRAME;
		if (m_fLastTick > 0 && now > m_fLastTick)
			dt = (now - m_fLastTick) * 0.001;
		m_fLastTick = now;

		vector vel = m_Move.GetVelocity();
		float speed = vel.Length();
		vector pos = owner.GetOrigin();

		// ---- 1. wait until the round is actually flying
		if (!m_bLaunched)
		{
			if (speed < m_fLaunchSpeed)
			{
				Schedule(IDLE_POLL_MS);
				return;
			}

			m_bLaunched = true;
			m_vLaunchPos = pos;
			m_fLaunchTime = now;
		}
		else if (speed < 1)
		{
			m_bDone = true;	// stopped without detonating (stuck or removed from simulation): nothing left to fuze
			return;
		}

		// ---- 2. arming: descending, old enough, far enough from the tube
		float vDown = -vel[1];
		float flightTime = (now - m_fLaunchTime) * 0.001;
		if (vDown <= 0 || flightTime < m_fArmTime || vector.Distance(pos, m_vLaunchPos) < m_fArmDistance)
		{
			float waitMs = 100;
			if (vDown < 0)
				waitMs = (-vDown / GRAVITY) * 500;	// half the remaining time to the apex

			Schedule(Math.Clamp(waitMs, 20, MAX_POLL_MS));
			return;
		}

		// ---- 3. height above whatever is under the bomb
		float height = pos[1] - SurfaceBelow(world, owner, pos);

		if (height > m_fCloseHeight)
		{
			// far above: sleep for most of the time it needs to reach the per-frame band
			float timeToBand = (height - m_fCloseHeight) / vDown;
			Schedule(Math.Clamp(timeToBand * 700, 0, MAX_POLL_MS));
			return;
		}

		// ---- 4. per-frame band: burst now if waiting one more frame would overshoot by more
		float frame = dt;
		if (frame <= 0 || frame > 0.1)
			frame = DEFAULT_FRAME;

		if (height - vDown * frame * 0.5 <= m_fBurstHeight)
		{
			Detonate(owner, height, "height");
			return;
		}

		// hillside, wall or tree ahead of the bomb along its flight path
		if (m_bTraceObjects)
		{
			float stepLength = speed * frame;
			float clear = PathClearance(world, owner, pos, vel, speed, m_fBurstHeight + stepLength);
			if (clear - stepLength * 0.5 <= m_fBurstHeight)
			{
				Detonate(owner, clear, "path");
				return;
			}
		}

		Schedule(0);
	}

	//------------------------------------------------------------------------------------------------
	//! World Y of the highest surface straight below pos: terrain, sea, and (with m_bTraceObjects) anything a projectile can hit
	protected float SurfaceBelow(BaseWorld world, IEntity owner, vector pos)
	{
		float surface = world.GetSurfaceY(pos[0], pos[2]);
		if (world.IsOcean())
			surface = Math.Max(surface, world.GetOceanHeight(pos[0], pos[2]));

		if (!m_bTraceObjects)
			return surface;

		float depth = pos[1] - surface;
		if (depth <= 0.1 || depth > MAX_TRACE_DEPTH)
			return surface;

		m_Trace.Start = pos;
		m_Trace.End = Vector(pos[0], surface - 0.1, pos[2]);
		m_Trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS | TraceFlags.OCEAN;
		m_Trace.Exclude = owner;
		m_Trace.LayerMask = EPhysicsLayerPresets.Projectile;

		float fraction = world.TraceMove(m_Trace, null);
		if (fraction < 1)
			surface = Math.Max(surface, pos[1] - fraction * (depth + 0.1));

		return surface;
	}

	//------------------------------------------------------------------------------------------------
	//! Free distance along the flight direction, capped at length
	protected float PathClearance(BaseWorld world, IEntity owner, vector pos, vector vel, float speed, float length)
	{
		if (speed < 1 || length <= 0)
			return length;

		vector dir = vel * (1.0 / speed);

		m_Trace.Start = pos;
		m_Trace.End = pos + dir * length;
		m_Trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS | TraceFlags.OCEAN;
		m_Trace.Exclude = owner;
		m_Trace.LayerMask = EPhysicsLayerPresets.Projectile;

		float fraction = world.TraceMove(m_Trace, null);
		if (fraction < 1)
			return fraction * length;

		return length;
	}

	//------------------------------------------------------------------------------------------------
	protected void Detonate(IEntity owner, float measured, string reason)
	{
		m_bDone = true;

		if (!owner || !m_Trigger || m_Trigger.WasTriggered())
			return;

		if (m_bDebug)
		{
			vector p = owner.GetOrigin();
			Print(string.Format("STC_AirburstFuzeComponent: burst (%1) at %2 m, %3 m from the tube, pos %4", reason, measured.ToString(lenDec: 1), vector.Distance(p, m_vLaunchPos).ToString(lenDec: 0), p.ToString()), LogLevel.NORMAL);
		}

		// Queue the check BEFORE triggering: the trigger may delete this entity inside OnUserTrigger
		// (OnDelete then removes the queued call). Nothing may touch members after OnUserTrigger.
		GetGame().GetCallqueue().CallLater(VerifyBurst, 100, false);
		m_Trigger.OnUserTrigger(owner);
	}

	//------------------------------------------------------------------------------------------------
	//! Still here and not triggered 100 ms later: arm the trigger explicitly (mine pattern) and try once more
	protected void VerifyBurst()
	{
		IEntity owner = GetOwner();
		if (!owner || !m_Trigger || m_Trigger.WasTriggered())
			return;

		if (m_iRetries > 0)
			return;

		m_iRetries++;
		Print("STC_AirburstFuzeComponent: OnUserTrigger did not set the trigger off - retrying after SetLive()", LogLevel.WARNING);
		m_Trigger.SetLive();
		m_Trigger.OnUserTrigger(owner);
	}
}
