//------------------------------------------------------------------------------
// STC attachment types.
// STC_AttachmentOpticsLongLas: the scope rail of the Thillium / Errant pattern
// long las. Only optics carrying this type fit that slot, and this type fits
// no vanilla rail. Same declaration pattern as vanilla Attachments_optics.c.
//------------------------------------------------------------------------------
class STC_AttachmentOpticsLongLasClass {}
STC_AttachmentOpticsLongLasClass STC_AttachmentOpticsLongLasSource;

class STC_AttachmentOpticsLongLas : AttachmentOptics
{
};

//------------------------------------------------------------------------------
// STC_AttachmentOpticsAccatran: the carry-handle mount of the Accatran Mk II
// marksman lasrifle. Only the Accatran 4x optic carries this type.
//------------------------------------------------------------------------------
class STC_AttachmentOpticsAccatranClass {}
STC_AttachmentOpticsAccatranClass STC_AttachmentOpticsAccatranSource;

class STC_AttachmentOpticsAccatran : AttachmentOptics
{
};
