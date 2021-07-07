#ifdef SLIC3RXS
#include <xsinit.h>

namespace Slic3r {

REGISTER_CLASS(ExPolygon, "ExPolygon");
REGISTER_CLASS(ExPolygonCollection, "ExPolygon::Collection");
REGISTER_CLASS(ExtrusionMultiPath, "ExtrusionMultiPath");
REGISTER_CLASS(ExtrusionPath, "ExtrusionPath");
REGISTER_CLASS(ExtrusionLoop, "ExtrusionLoop");
REGISTER_CLASS(ExtrusionEntityCollection, "ExtrusionPath::Collection");
REGISTER_CLASS(ExtrusionSimulator, "ExtrusionSimulator");
REGISTER_CLASS(Filler, "Filler");
REGISTER_CLASS(Flow, "Flow");
REGISTER_CLASS(CoolingBuffer, "GCode::CoolingBuffer");
REGISTER_CLASS(GCode, "GCode");
REGISTER_CLASS(Layer, "Layer");
REGISTER_CLASS(SupportLayer, "Layer::Support");
REGISTER_CLASS(LayerRegion, "Layer::Region");
REGISTER_CLASS(Line, "Line");
REGISTER_CLASS(Linef3, "Linef3");
REGISTER_CLASS(PerimeterGenerator, "Layer::PerimeterGenerator");
REGISTER_CLASS(PlaceholderParser, "GCode::PlaceholderParser");
REGISTER_CLASS(Polygon, "Polygon");
REGISTER_CLASS(Polyline, "Polyline");
REGISTER_CLASS(PolylineCollection, "Polyline::Collection");
REGISTER_CLASS(Print, "Print");
REGISTER_CLASS(PrintObject, "Print::Object");
REGISTER_CLASS(PrintRegion, "Print::Region");
REGISTER_CLASS(Model, "Model");
REGISTER_CLASS(ModelMaterial, "Model::Material");
REGISTER_CLASS(ModelObject, "Model::Object");
REGISTER_CLASS(ModelVolume, "Model::Volume");
REGISTER_CLASS(ModelInstance, "Model::Instance");
REGISTER_CLASS(BoundingBox, "Geometry::BoundingBox");
REGISTER_CLASS(BoundingBoxf, "Geometry::BoundingBoxf");
REGISTER_CLASS(BoundingBoxf3, "Geometry::BoundingBoxf3");
REGISTER_CLASS(BridgeDetector, "BridgeDetector");
REGISTER_CLASS(Point, "Point");
__REGISTER_CLASS(Vec2d, "Pointf");
__REGISTER_CLASS(Vec3d, "Pointf3");
REGISTER_CLASS(DynamicPrintConfig, "Config");
REGISTER_CLASS(StaticPrintConfig, "Config::Static");
REGISTER_CLASS(PrintObjectConfig, "Config::PrintObject");
REGISTER_CLASS(PrintRegionConfig, "Config::PrintRegion");
REGISTER_CLASS(GCodeConfig, "Config::GCode");
REGISTER_CLASS(PrintConfig, "Config::Print");
REGISTER_CLASS(FullPrintConfig, "Config::Full");
REGISTER_CLASS(Surface, "Surface");
REGISTER_CLASS(SurfaceCollection, "Surface::Collection");
REGISTER_CLASS(PrintObjectSupportMaterial, "Print::SupportMaterial2");
REGISTER_CLASS(TriangleMesh, "TriangleMesh");

SV* ConfigBase__as_hash(ConfigBase* THIS)
{
    HV* hv = newHV();    
    for (auto &key : THIS->keys())
        (void)hv_store(hv, key.c_str(), key.length(), ConfigBase__get(THIS, key), 0);
    return newRV_noinc((SV*)hv);
}

SV* ConfigBase__get(ConfigBase* THIS, const t_config_option_key &opt_key)
{
    ConfigOption *opt = THIS->option(opt_key, false);
    return (opt == nullptr) ? 
        &PL_sv_undef :
        ConfigOption_to_SV(*opt, *THIS->def()->get(opt_key));
}

SV* ConfigOption_to_SV(const ConfigOption &opt, const ConfigOptionDef &def)
{
    switch (def.type) {
    case coFloat:
    case coPercent:
        return newSVnv(static_cast<const ConfigOptionFloat*>(&opt)->value);
    case coFloats:
    case coPercents:
    {
        auto optv = static_cast<const ConfigOptionFloats*>(&opt);
        AV* av = newAV();
        av_fill(av, optv->values.size()-1);
        for (const double &v : optv->values)
            av_store(av, &v - optv->values.data(), newSVnv(v));
        return newRV_noinc((SV*)av);
    }
    case coInt:
        return newSViv(static_cast<const ConfigOptionInt*>(&opt)->value);
    case coInts:
    {
        auto optv = static_cast<const ConfigOptionInts*>(&opt);
        AV* av = newAV();
        av_fill(av, optv->values.size()-1);
        for (const int &v : optv->values)
            av_store(av, &v - optv->values.data(), newSViv(v));
        return newRV_noinc((SV*)av);
    }
    case coString:
    {
        auto optv = static_cast<const ConfigOptionString*>(&opt);
        // we don't serialize() because that would escape newlines
        return newSVpvn_utf8(optv->value.c_str(), optv->value.length(), true);
    }
    case coStrings:
    {
        auto optv = static_cast<const ConfigOptionStrings*>(&opt);
        AV* av = newAV();
        av_fill(av, optv->values.size()-1);
        for (const std::string &v : optv->values)
            av_store(av, &v - optv->values.data(), newSVpvn_utf8(v.c_str(), v.length(), true));
        return newRV_noinc((SV*)av);
    }
    case coPoint:
        return perl_to_SV_clone_ref(static_cast<const ConfigOptionPoint*>(&opt)->value);
    case coPoint3:
        return perl_to_SV_clone_ref(static_cast<const ConfigOptionPoint3*>(&opt)->value);
    case coPoints:
    {
        auto optv = static_cast<const ConfigOptionPoints*>(&opt);
        AV* av = newAV();
        av_fill(av, optv->values.size()-1);
        for (const Vec2d &v : optv->values)
            av_store(av, &v - optv->values.data(), perl_to_SV_clone_ref(v));
        return newRV_noinc((SV*)av);
    }
    case coBool:
        return newSViv(static_cast<const ConfigOptionBool*>(&opt)->value ? 1 : 0);
    case coBools:
    {
        auto optv = static_cast<const ConfigOptionBools*>(&opt);
        AV* av = newAV();
        av_fill(av, optv->values.size()-1);
        for (size_t i = 0; i < optv->values.size(); ++ i)
            av_store(av, i, newSViv(optv->values[i] ? 1 : 0));
        return newRV_noinc((SV*)av);
    }
    default:
        std::string serialized = opt.serialize();
        return newSVpvn_utf8(serialized.c_str(), serialized.length(), true);
    }
}

SV* ConfigBase__get_at(ConfigBase* THIS, const t_config_option_key &opt_key, size_t i)
{
    ConfigOption* opt = THIS->option(opt_key, false);
    if (opt == nullptr)
        return &PL_sv_undef;
    
    const ConfigOptionDef* def = THIS->def()->get(opt_key);
    switch (def->type) {
    case coFloats:
    case coPercents:
        return newSVnv(static_cast<ConfigOptionFloats*>(opt)->get_at(i));
    case coInts:
        return newSViv(static_cast<ConfigOptionInts*>(opt)->get_at(i));
    case coStrings:
    {
        // we don't serialize() because that would escape newlines
        const std::string &val = static_cast<ConfigOptionStrings*>(opt)->get_at(i);
        return newSVpvn_utf8(val.c_str(), val.length(), true);
    }
    case coPoints:
        return perl_to_SV_clone_ref(static_cast<ConfigOptionPoints*>(opt)->get_at(i));
    case coBools:
        return newSViv(static_cast<ConfigOptionBools*>(opt)->get_at(i) ? 1 : 0);
    default:
        return &PL_sv_undef;
    }
}

bool ConfigBase__set(ConfigBase* THIS, const t_config_option_key &opt_key, SV* value)
{
    ConfigOption* opt = THIS->option(opt_key, true);
    if (opt == nullptr)
        CONFESS("Trying to set non-existing option");
    const ConfigOptionDef* def = THIS->def()->get(opt_key);
    if (opt->type() != def->type)
        CONFESS("Option type is different from the definition");
    switch (def->type) {
    case coFloat:
        if (!looks_like_number(value))
            return false;
        static_cast<ConfigOptionFloat*>(opt)->value = SvNV(value);
        break;
    case coFloats:
    {
        std::vector<double> &values = static_cast<ConfigOptionFloats*>(opt)->values;
        AV* av = (AV*)SvRV(value);
        const size_t len = av_len(av)+1;
        values.clear();
        values.reserve(len);
        for (size_t i = 0; i < len; ++ i) {
            SV** elem = av_fetch(av, i, 0);
            if (elem == NULL || !looks_like_number(*elem)) return false;
            values.emplace_back(SvNV(*elem));
        }
        break;
    }
    case coPercents:
    {
        std::vector<double> &values = static_cast<ConfigOptionPercents*>(opt)->values;
        AV* av = (AV*)SvRV(value);
        const size_t len = av_len(av)+1;
        values.clear();
        values.reserve(len);
        for (size_t i = 0; i < len; i++) {
            SV** elem = av_fetch(av, i, 0);
            if (elem == NULL || !looks_like_number(*elem)) return false;
            values.emplace_back(SvNV(*elem));
        }
        break;
    }
    case coInt:
        if (!looks_like_number(value)) return false;
        static_cast<ConfigOptionInt*>(opt)->value = SvIV(value);
        break;
    case coInts:
    {
        std::vector<int> &values = static_cast<ConfigOptionInts*>(opt)->values;
        AV* av = (AV*)SvRV(value);
        const size_t len = av_len(av)+1;
        values.clear();
        values.reserve(len);
        for (size_t i = 0; i < len; i++) {
            SV** elem = av_fetch(av, i, 0);
            if (elem == NULL || !looks_like_number(*elem)) return false;
            values.emplace_back(SvIV(*elem));
        }
        break;
    }
    case coString:
        static_cast<ConfigOptionString*>(opt)->value = std::string(SvPV_nolen(value), SvCUR(value));
        break;
    case coStrings:
    {
        std::vector<std::string> &values = static_cast<ConfigOptionStrings*>(opt)->values;
        AV* av = (AV*)SvRV(value);
        const size_t len = av_len(av)+1;
        values.clear();
        values.reserve(len);
        for (size_t i = 0; i < len; i++) {
            SV** elem = av_fetch(av, i, 0);
            if (elem == NULL) return false;
            values.emplace_back(std::string(SvPV_nolen(*elem), SvCUR(*elem)));
        }
        break;
    }
    case coPoint:
        return from_SV_check(value, &static_cast<ConfigOptionPoint*>(opt)->value);
//    case coPoint3:        
        // not gonna fix it, die Perl die!
//        return from_SV_check(value, &static_cast<ConfigOptionPoint3*>(opt)->value);
    case coPoints:
    {
        std::vector<Vec2d> &values = static_cast<ConfigOptionPoints*>(opt)->values;
        AV* av = (AV*)SvRV(value);
        const size_t len = av_len(av)+1;
        values.clear();
        values.reserve(len);
        for (size_t i = 0; i < len; i++) {
            SV** elem = av_fetch(av, i, 0);
            Vec2d point(Vec2d::Zero());
            if (elem == NULL || !from_SV_check(*elem, &point)) return false;
            values.emplace_back(point);
        }
        break;
    }
    case coBool:
        static_cast<ConfigOptionBool*>(opt)->value = SvTRUE(value);
        break;
    case coBools:
    {
        std::vector<unsigned char> &values = static_cast<ConfigOptionBools*>(opt)->values;
        AV* av = (AV*)SvRV(value);
        const size_t len = av_len(av)+1;
        values.clear();
        values.reserve(len);
        for (size_t i = 0; i < len; i++) {
            SV** elem = av_fetch(av, i, 0);
            if (elem == NULL) return false;
            values.emplace_back(SvTRUE(*elem));
        }
        break;
    }
    default:
        if (! opt->deserialize(std::string(SvPV_nolen(value)), ForwardCompatibilitySubstitutionRule::Disable))
            return false;
    }
    return true;
}

/* This method is implemented as a workaround for this typemap bug:
   https://rt.cpan.org/Public/Bug/Display.html?id=94110 */
bool ConfigBase__set_deserialize(ConfigBase* THIS, const t_config_option_key &opt_key, SV* str)
{
    size_t len;
    const char * c = SvPV(str, len);
    std::string value(c, len);
    ConfigSubstitutionContext ctxt{ ForwardCompatibilitySubstitutionRule::Disable };
    return THIS->set_deserialize_nothrow(opt_key, value, ctxt);
}

void ConfigBase__set_ifndef(ConfigBase* THIS, const t_config_option_key &opt_key, SV* value, bool deserialize)
{
    if (THIS->has(opt_key))
        return;
    if (deserialize)
        ConfigBase__set_deserialize(THIS, opt_key, value);
    else
        ConfigBase__set(THIS, opt_key, value);
}

bool StaticConfig__set(StaticConfig* THIS, const t_config_option_key &opt_key, SV* value)
{
    const ConfigOptionDef* optdef = THIS->def()->get(opt_key);
    if (optdef->shortcut.empty())
        return ConfigBase__set(THIS, opt_key, value);
    for (const t_config_option_key &key : optdef->shortcut)
        if (! StaticConfig__set(THIS, key, value))
            return false;
    return true;
}

SV* to_AV(ExPolygon* expolygon)
{
    const unsigned int num_holes = expolygon->holes.size();
    AV* av = newAV();
    av_extend(av, num_holes);  // -1 +1
    
    av_store(av, 0, perl_to_SV_ref(expolygon->contour));
    
    for (unsigned int i = 0; i < num_holes; i++) {
        av_store(av, i+1, perl_to_SV_ref(expolygon->holes[i]));
    }
    return newRV_noinc((SV*)av);
}

SV* to_SV_pureperl(const ExPolygon* expolygon)
{
    const unsigned int num_holes = expolygon->holes.size();
    AV* av = newAV();
    av_extend(av, num_holes);  // -1 +1
    av_store(av, 0, to_SV_pureperl(&expolygon->contour));
    for (unsigned int i = 0; i < num_holes; i++) {
        av_store(av, i+1, to_SV_pureperl(&expolygon->holes[i]));
    }
    return newRV_noinc((SV*)av);
}

void from_SV(SV* expoly_sv, ExPolygon* expolygon)
{
    AV* expoly_av = (AV*)SvRV(expoly_sv);
    const unsigned int num_polygons = av_len(expoly_av)+1;
    expolygon->holes.resize(num_polygons-1);
    
    SV** polygon_sv = av_fetch(expoly_av, 0, 0);
    from_SV(*polygon_sv, &expolygon->contour);
    for (unsigned int i = 0; i < num_polygons-1; i++) {
        polygon_sv = av_fetch(expoly_av, i+1, 0);
        from_SV(*polygon_sv, &expolygon->holes[i]);
    }
}

void from_SV_check(SV* expoly_sv, ExPolygon* expolygon)
{
    if (sv_isobject(expoly_sv) && (SvTYPE(SvRV(expoly_sv)) == SVt_PVMG)) {
        if (!sv_isa(expoly_sv, perl_class_name(expolygon)) && !sv_isa(expoly_sv, perl_class_name_ref(expolygon)))
          CONFESS("Not a valid %s object", perl_class_name(expolygon));
        // a XS ExPolygon was supplied
        *expolygon = *(ExPolygon *)SvIV((SV*)SvRV( expoly_sv ));
    } else {
        // a Perl arrayref was supplied
        from_SV(expoly_sv, expolygon);
    }
}

void from_SV(SV* line_sv, Line* THIS)
{
    AV* line_av = (AV*)SvRV(line_sv);
    from_SV_check(*av_fetch(line_av, 0, 0), &THIS->a);
    from_SV_check(*av_fetch(line_av, 1, 0), &THIS->b);
}

void from_SV_check(SV* line_sv, Line* THIS)
{
    if (sv_isobject(line_sv) && (SvTYPE(SvRV(line_sv)) == SVt_PVMG)) {
        if (!sv_isa(line_sv, perl_class_name(THIS)) && !sv_isa(line_sv, perl_class_name_ref(THIS)))
            CONFESS("Not a valid %s object", perl_class_name(THIS));
        *THIS = *(Line*)SvIV((SV*)SvRV( line_sv ));
    } else {
        from_SV(line_sv, THIS);
    }
}

SV* to_AV(Line* THIS)
{
    AV* av = newAV();
    av_extend(av, 1);
    
    av_store(av, 0, perl_to_SV_ref(THIS->a));
    av_store(av, 1, perl_to_SV_ref(THIS->b));
    
    return newRV_noinc((SV*)av);
}

SV* to_SV_pureperl(const Line* THIS)
{
    AV* av = newAV();
    av_extend(av, 1);
    av_store(av, 0, to_SV_pureperl(&THIS->a));
    av_store(av, 1, to_SV_pureperl(&THIS->b));
    return newRV_noinc((SV*)av);
}

void from_SV(SV* poly_sv, MultiPoint* THIS)
{
    AV* poly_av = (AV*)SvRV(poly_sv);
    const unsigned int num_points = av_len(poly_av)+1;
    THIS->points.resize(num_points);
    
    for (unsigned int i = 0; i < num_points; i++) {
        SV** point_sv = av_fetch(poly_av, i, 0);
        from_SV_check(*point_sv, &THIS->points[i]);
    }
}

void from_SV_check(SV* poly_sv, MultiPoint* THIS)
{
    if (sv_isobject(poly_sv) && (SvTYPE(SvRV(poly_sv)) == SVt_PVMG)) {
        *THIS = *(MultiPoint*)SvIV((SV*)SvRV( poly_sv ));
    } else {
        from_SV(poly_sv, THIS);
    }
}

SV* to_AV(MultiPoint* THIS)
{
    const unsigned int num_points = THIS->points.size();
    AV* av = newAV();
    if (num_points > 0) av_extend(av, num_points-1);
    for (unsigned int i = 0; i < num_points; i++) {
        av_store(av, i, perl_to_SV_ref(THIS->points[i]));
    }
    return newRV_noinc((SV*)av);
}

SV* to_SV_pureperl(const MultiPoint* THIS)
{
    const unsigned int num_points = THIS->points.size();
    AV* av = newAV();
    if (num_points > 0) av_extend(av, num_points-1);
    for (unsigned int i = 0; i < num_points; i++) {
        av_store(av, i, to_SV_pureperl(&THIS->points[i]));
    }
    return newRV_noinc((SV*)av);
}

void from_SV_check(SV* poly_sv, Polygon* THIS)
{
    if (sv_isobject(poly_sv) && !sv_isa(poly_sv, perl_class_name(THIS)) && !sv_isa(poly_sv, perl_class_name_ref(THIS)))
        CONFESS("Not a valid %s object", perl_class_name(THIS));
    
    from_SV_check(poly_sv, (MultiPoint*)THIS);
}

void from_SV_check(SV* poly_sv, Polyline* THIS)
{
    if (!sv_isa(poly_sv, perl_class_name(THIS)) && !sv_isa(poly_sv, perl_class_name_ref(THIS)))
        CONFESS("Not a valid %s object", perl_class_name(THIS));
    
    from_SV_check(poly_sv, (MultiPoint*)THIS);
}

SV* to_SV_pureperl(const Point* THIS)
{
    AV* av = newAV();
    av_fill(av, 1);
    av_store(av, 0, newSViv((*THIS)(0)));
    av_store(av, 1, newSViv((*THIS)(1)));
    return newRV_noinc((SV*)av);
}

void from_SV(SV* point_sv, Point* point)
{
    AV* point_av = (AV*)SvRV(point_sv);
    // get a double from Perl and round it, otherwise
    // it would get truncated
    (*point) = Point(SvNV(*av_fetch(point_av, 0, 0)), SvNV(*av_fetch(point_av, 1, 0)));
}

void from_SV_check(SV* point_sv, Point* point)
{
    if (sv_isobject(point_sv) && (SvTYPE(SvRV(point_sv)) == SVt_PVMG)) {
        if (!sv_isa(point_sv, perl_class_name(point)) && !sv_isa(point_sv, perl_class_name_ref(point)))
            CONFESS("Not a valid %s object (got %s)", perl_class_name(point), HvNAME(SvSTASH(SvRV(point_sv))));
        *point = *(Point*)SvIV((SV*)SvRV( point_sv ));
    } else {
        from_SV(point_sv, point);
    }
}

SV* to_SV_pureperl(const Vec2d* point)
{
    AV* av = newAV();
    av_fill(av, 1);
    av_store(av, 0, newSVnv((*point)(0)));
    av_store(av, 1, newSVnv((*point)(1)));
    return newRV_noinc((SV*)av);
}

bool from_SV(SV* point_sv, Vec2d* point)
{
    AV* point_av = (AV*)SvRV(point_sv);
    SV* sv_x = *av_fetch(point_av, 0, 0);
    SV* sv_y = *av_fetch(point_av, 1, 0);
    if (!looks_like_number(sv_x) || !looks_like_number(sv_y)) return false;
    
    *point = Vec2d(SvNV(sv_x), SvNV(sv_y));
    return true;
}

bool from_SV_check(SV* point_sv, Vec2d* point)
{
    if (sv_isobject(point_sv) && (SvTYPE(SvRV(point_sv)) == SVt_PVMG)) {
        if (!sv_isa(point_sv, perl_class_name(point)) && !sv_isa(point_sv, perl_class_name_ref(point)))
            CONFESS("Not a valid %s object (got %s)", perl_class_name(point), HvNAME(SvSTASH(SvRV(point_sv))));
        *point = *(Vec2d*)SvIV((SV*)SvRV( point_sv ));
        return true;
    } else {
        return from_SV(point_sv, point);
    }
}

void from_SV_check(SV* surface_sv, Surface* THIS)
{
    if (!sv_isa(surface_sv, perl_class_name(THIS)) && !sv_isa(surface_sv, perl_class_name_ref(THIS)))
        CONFESS("Not a valid %s object", perl_class_name(THIS));
    // a XS Surface was supplied
    *THIS = *(Surface *)SvIV((SV*)SvRV( surface_sv ));
}

SV* to_SV(TriangleMesh* THIS)
{
    SV* sv = newSV(0);
    sv_setref_pv( sv, perl_class_name(THIS), (void*)THIS );
    return sv;
}

}
#endif
