[ComponentEditorProps(category: "STC Modules", description: "Hitscan beam that applies instantaneous direct damage. Pair with a 0-damage dummy round in the cell so damage is weapon-defined, not cell-defined.")]
class STC_HitscanLasgunModifier : ScriptedWeaponAimModifier
{
    [Attribute("", UIWidgets.ResourceNamePicker, "Beam Mesh Prefab")]
    protected ResourceName m_BeamPrefab;

    [Attribute("1000", UIWidgets.EditBox, "Max range of the beam in meters.")]
    protected float m_fMaxRange;

    [Attribute("0 0.05 0.5", UIWidgets.Coords, "Local offset (Right Up Forward) to align the beam start with the barrel tip.")]
    protected vector m_vMuzzleOffset;

    [Attribute("50", UIWidgets.EditBox, "Base damage applied to the hit entity.")]
    protected float m_fBaseDamage;

    [Attribute("1", UIWidgets.CheckBox, "Apply hitscan damage. Disable to use this modifier as a beam VISUAL only (damage stays on the projectile).")]
    protected bool m_bApplyDamage;

    [Attribute("2", UIWidgets.EditBox, "Length in meters of the beam mesh at scale 1 (used to scale the beam to the hit distance).")]
    protected float m_fBeamMeshLength;

    protected BaseMuzzleComponent m_Muzzle;
    protected RplComponent m_RplComponent;
    protected IEntity m_WeaponEnt;

    override void OnInit(IEntity weaponEnt)
    {
        m_WeaponEnt = weaponEnt;
        WeaponComponent weaponComp = WeaponComponent.Cast(weaponEnt.FindComponent(WeaponComponent));
        if (weaponComp)
        {
            m_Muzzle = weaponComp.GetCurrentMuzzle();
        }
        m_RplComponent = RplComponent.Cast(weaponEnt.FindComponent(RplComponent));
    }

    override void OnWeaponFired()
    {
        if (!m_Muzzle || m_BeamPrefab.IsEmpty() || !m_WeaponEnt)
            return;

        vector mat[4];
        m_WeaponEnt.GetTransform(mat);
        vector startPos = mat[3] + (mat[0] * m_vMuzzleOffset[0]) + (mat[1] * m_vMuzzleOffset[1]) + (mat[2] * m_vMuzzleOffset[2]);
        vector dir = mat[2];
        vector endPos = startPos + (dir * m_fMaxRange);

        TraceParam trace = new TraceParam();
        trace.Start = startPos;
        trace.End = endPos;
        trace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;

        IEntity shooter = m_WeaponEnt.GetRootParent();
        trace.Exclude = shooter;

        float hitFraction = GetGame().GetWorld().TraceMove(trace, null);

        vector actualHitPos = startPos + (dir * (m_fMaxRange * hitFraction));
        float distance = vector.Distance(startPos, actualHitPos);

        // Visuals spawn locally for whoever runs this event (client or server)
        SpawnBeamMeshLocal(startPos, actualHitPos, distance);

        // Damage executes only on the authority
        if (m_bApplyDamage && m_RplComponent && m_RplComponent.IsMaster())
        {
            if (hitFraction < 1.0 && trace.TraceEnt)
            {
                ApplyHitscanDamage(trace.TraceEnt, shooter);
            }
        }
    }

    protected void ApplyHitscanDamage(IEntity hitEntity, IEntity shooter)
    {
        SCR_DamageManagerComponent damageManager = SCR_DamageManagerComponent.Cast(hitEntity.FindComponent(SCR_DamageManagerComponent));

        if (damageManager)
        {
            Instigator instigator = Instigator.CreateInstigator(shooter);

            BaseDamageContext context = new BaseDamageContext();
            context.damageValue = m_fBaseDamage;
            context.damageType = EDamageType.KINETIC;
            context.instigator = instigator;
            context.hitEntity = hitEntity;

            damageManager.HandleDamage(context);
        }
    }

    protected void SpawnBeamMeshLocal(vector startPos, vector hitPos, float distance)
    {
        vector dir = vector.Direction(startPos, hitPos).Normalized();

        EntitySpawnParams params = new EntitySpawnParams();
        params.TransformMode = ETransformMode.WORLD;

        Math3D.DirectionAndUpMatrix(dir, vector.Up, params.Transform);
        params.Transform[3] = startPos;

        // SpawnEntityPrefabLocal so the server does not attempt to network the mesh
        IEntity beamEntity = GetGame().SpawnEntityPrefabLocal(Resource.Load(m_BeamPrefab), GetGame().GetWorld(), params);

        if (beamEntity)
        {
            vector mat[4];
            beamEntity.GetTransform(mat);

            mat[2] = mat[2] * (distance / m_fBeamMeshLength);

            beamEntity.SetTransform(mat);

            GetGame().GetCallqueue().CallLater(DeleteBeam, 50, false, beamEntity);
        }
    }

    protected void DeleteBeam(IEntity beamEntity)
    {
        if (beamEntity)
        {
            delete beamEntity;
        }
    }
}
