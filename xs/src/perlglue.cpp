#ifdef SLIC3RXS
#include <xsinit.h>

namespace Slic3r {

REGISTER_CLASS(ExPolygon, "ExPolygon");
REGISTER_CLASS(ExPolygonCollection, "ExPolygon::Collection");
REGISTER_CLASS(ExtrusionMultiPath, "ExtrusionMultiPath");
REGISTER_CLASS(ExtrusionPath, "ExtrusionPath");
REGISTER_CLASS(ExtrusionLoop, "ExtrusionLoop");
// there is no ExtrusionLoop::Collection or ExtrusionEntity::Collection
REGISTER_CLASS(ExtrusionEntityCollection, "ExtrusionPath::Collection");
REGISTER_CLASS(ExtrusionSimulator, "ExtrusionSimulator");
REGISTER_CLASS(Filler, "Filler");
REGISTER_CLASS(Flow, "Flow");
REGISTER_CLASS(CoolingBuffer, "GCode::CoolingBuffer");
REGISTER_CLASS(GCode, "GCode");
REGISTER_CLASS(GCodeSender, "GCode::Sender");
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
REGISTER_CLASS(MotionPlanner, "MotionPlanner");
REGISTER_CLASS(BoundingBox, "Geometry::BoundingBox");
REGISTER_CLASS(BoundingBoxf, "Geometry::BoundingBoxf");
REGISTER_CLASS(BoundingBoxf3, "Geometry::BoundingBoxf3");
REGISTER_CLASS(BridgeDetector, "BridgeDetector");
REGISTER_CLASS(Point, "Point");
REGISTER_CLASS(Point3, "Point3");
REGISTER_CLASS(Pointf, "Pointf");
REGISTER_CLASS(Pointf3, "Pointf3");
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
REGISTER_CLASS(GLShader, "GUI::_3DScene::GLShader");
REGISTER_CLASS(GLVolume, "GUI::_3DScene::GLVolume");
REGISTER_CLASS(GLVolumeCollection, "GUI::_3DScene::GLVolume::Collection");
REGISTER_CLASS(Preset, "GUI::Preset");
REGISTER_CLASS(PresetCollection, "GUI::PresetCollection");
REGISTER_CLASS(PresetBundle, "GUI::PresetBundle");

SV*
ConfigBase__as_hash(ConfigBase* THIS) {
    HV* hv = newHV();
    
    t_config_option_keys opt_keys = THIS->keys();
    for (t_config_option_keys::const_iterator it = opt_keys.begin(); it != opt_keys.end(); ++it)
        (void)hv_store( hv, it->c_str(), it->length(), ConfigBase__get(THIS, *it), 0 );
    
    return newRV_noinc((SV*)hv);
}

SV*
ConfigBase__get(ConfigBase* THIS, const t_config_option_key &opt_key) {
    ConfigOption* opt = THIS->option(opt_key);
    if (opt == NULL) return &PL_sv_undef;
    
    const ConfigOptionDef* def = THIS->def->get(opt_key);
    return ConfigOption_to_SV(*opt, *def);
}

SV*
ConfigOption_to_SV(const ConfigOption &opt, const ConfigOptionDef &def) {
    if (def.type == coFloat) {
        const ConfigOptionFloat* optv = dynamic_cast<const ConfigOptionFloat*>(&opt);
        return newSVnv(optv->value);
    } else if (def.type == coFloats) {
        const ConfigOptionFloats* optv = dynamic_cast<const ConfigOptionFloats*>(&opt);
        AV* av = newAV();
        av_fill(av, optv->values.size()-1);
        for (std::vector<double>::const_iterator it = optv->values.begin(); it != optv->values.end(); ++it)
            av_store(av, it - optv->values.begin(), newSVnv(*it));
        return newRV_noinc((SV*)av);
    } else if (def.type == coPercent) {
        const ConfigOptionPercent* optv = dynamic_cast<const ConfigOptionPercent*>(&opt);
        return newSVnv(optv->value);
    } else if (def.type == coPercents) {
        const ConfigOptionPercents* optv = dynamic_cast<const ConfigOptionPercents*>(&opt);
        AV* av = newAV();
        av_fill(av, optv->values.size()-1);
        for (const double &v : optv->values)
            av_store(av, &v - &optv->values.front(), newSVnv(v));
        return newRV_noinc((SV*)av);
    } else if (def.type == coInt) {
        const ConfigOptionInt* optv = dynamic_cast<const ConfigOptionInt*>(&opt);
        return newSViv(optv->value);
    } else if (def.type == coInts) {
        const ConfigOptionInts* optv = dynamic_cast<const ConfigOptionInts*>(&opt);
        AV* av = newAV();
        av_fill(av, optv->values.size()-1);
        for (std::vector<int>::const_iterator it = optv->values.begin(); it != optv->values.end(); ++it)
            av_store(av, it - optv->values.begin(), newSViv(*it));
        return newRV_noinc((SV*)av);
    } else if (def.type == coString) {
        const ConfigOptionString* optv = dynamic_cast<const ConfigOptionString*>(&opt);
        // we don't serialize() because that would escape newlines
        return newSVpvn_utf8(optv->value.c_str(), optv->value.length(), true);
    } else if (def.type == coStrings) {
        const ConfigOptionStrings* optv = dynamic_cast<const ConfigOptionStrings*>(&opt);
        AV* av = newAV();
        av_fill(av, optv->values.size()-1);
        for (std::vector<std::string>::const_iterator it = optv->values.begin(); it != optv->values.end(); ++it)
            av_store(av, it - optv->values.begin(), newSVpvn_utf8(it->c_str(), it->length(), true));
        return newRV_noinc((SV*)av);
    } else if (def.type == coPoint) {
        const ConfigOptionPoint* optv = dynamic_cast<const ConfigOptionPoint*>(&opt);
        return perl_to_SV_clone_ref(optv->value);
    } else if (def.type == coPoints) {
        const ConfigOptionPoints* optv = dynamic_cast<const ConfigOptionPoints*>(&opt);
        AV* av = newAV();
        av_fill(av, optv->values.size()-1);
        for (Pointfs::const_iterator it = optv->values.begin(); it != optv->values.end(); ++it)
            av_store(av, it - optv->values.begin(), perl_to_SV_clone_ref(*it));
        return newRV_noinc((SV*)av);
    } else if (def.type == coBool) {
        const ConfigOptionBool* optv = dynamic_cast<const ConfigOptionBool*>(&opt);
        return newSViv(optv->value ? 1 : 0);
    } else if (def.type == coBools) {
        const ConfigOptionBools* optv = dynamic_cast<const ConfigOptionBools*>(&opt);
        AV* av = newAV();
        av_fill(av, optv->values.size()-1);
        for (size_t i = 0; i < optv->values.size(); ++ i)
            av_store(av, i, newSViv(optv->values[i] ? 1 : 0));
        return newRV_noinc((SV*)av);
    } else {
        std::string serialized = opt.serialize();
        return newSVpvn_utf8(serialized.c_str(), serialized.length(), true);
    }
}

SV*
ConfigBase__get_at(ConfigBase* THIS, const t_config_option_key &opt_key, size_t i) {
    ConfigOption* opt = THIS->option(opt_key);
    if (opt == NULL) return &PL_sv_undef;
    
    const ConfigOptionDef* def = THIS->def->get(opt_key);
    if (def->type == coFloats || def->type == coPercents) {
        ConfigOptionFloats* optv = dynamic_cast<ConfigOptionFloats*>(opt);
        return newSVnv(optv->get_at(i));
    } else if (def->type == coInts) {
        ConfigOptionInts* optv = dynamic_cast<ConfigOptionInts*>(opt);
        return newSViv(optv->get_at(i));
    } else if (def->type == coStrings) {
        ConfigOptionStrings* optv = dynamic_cast<ConfigOptionStrings*>(opt);
        // we don't serialize() because that would escape newlines
        std::string val = optv->get_at(i);
        return newSVpvn_utf8(val.c_str(), val.length(), true);
    } else if (def->type == coPoints) {
        ConfigOptionPoints* optv = dynamic_cast<ConfigOptionPoints*>(opt);
        return perl_to_SV_clone_ref(optv->get_at(i));
    } else if (def->type == coBools) {
        ConfigOptionBools* optv = dynamic_cast<ConfigOptionBools*>(opt);
        return newSViv(optv->get_at(i) ? 1 : 0);
    } else {
        return &PL_sv_undef;
    }
}

bool
ConfigBase__set(ConfigBase* THIS, const t_config_option_key &opt_key, SV* value) {
    ConfigOption* opt = THIS->option(opt_key, true);
    if (opt == NULL) CONFESS("Trying to set non-existing option");
    
    const ConfigOptionDef* def = THIS->def->get(opt_key);
    if (def->type == coFloat) {
        if (!looks_like_number(value)) return false;
        ConfigOptionFloat* optv = dynamic_cast<ConfigOptionFloat*>(opt);
        optv->value = SvNV(value);
    } else if (def->type == coFloats) {
        ConfigOptionFloats* optv = dynamic_cast<ConfigOptionFloats*>(opt);
        std::vector<double> values;
        AV* av = (AV*)SvRV(value);
        const size_t len = av_len(av)+1;
        for (size_t i = 0; i < len; i++) {
            SV** elem = av_fetch(av, i, 0);
            if (elem == NULL || !looks_like_number(*elem)) return false;
            values.push_back(SvNV(*elem));
        }
        optv->values = values;
    } else if (def->type == coPercents) {
        ConfigOptionPercents* optv = dynamic_cast<ConfigOptionPercents*>(opt);
        std::vector<double> values;
        AV* av = (AV*)SvRV(value);
        const size_t len = av_len(av)+1;
        for (size_t i = 0; i < len; i++) {
            SV** elem = av_fetch(av, i, 0);
            if (elem == NULL || !looks_like_number(*elem)) return false;
            values.push_back(SvNV(*elem));
        }
        optv->values = values;
    } else if (def->type == coInt) {
        if (!looks_like_number(value)) return false;
        ConfigOptionInt* optv = dynamic_cast<ConfigOptionInt*>(opt);
        optv->value = SvIV(value);
    } else if (def->type == coInts) {
        ConfigOptionInts* optv = dynamic_cast<ConfigOptionInts*>(opt);
        std::vector<int> values;
        AV* av = (AV*)SvRV(value);
        const size_t len = av_len(av)+1;
        for (size_t i = 0; i < len; i++) {
            SV** elem = av_fetch(av, i, 0);
            if (elem == NULL || !looks_like_number(*elem)) return false;
            values.push_back(SvIV(*elem));
        }
        optv->values = values;
    } else if (def->type == coString) {
        ConfigOptionString* optv = dynamic_cast<ConfigOptionString*>(opt);
        optv->value = std::string(SvPV_nolen(value), SvCUR(value));
    } else if (def->type == coStrings) {
        ConfigOptionStrings* optv = dynamic_cast<ConfigOptionStrings*>(opt);
        optv->values.clear();
        AV* av = (AV*)SvRV(value);
        const size_t len = av_len(av)+1;
        for (size_t i = 0; i < len; i++) {
            SV** elem = av_fetch(av, i, 0);
            if (elem == NULL) return false;
            optv->values.push_back(std::string(SvPV_nolen(*elem), SvCUR(*elem)));
        }
    } else if (def->type == coPoint) {
        ConfigOptionPoint* optv = dynamic_cast<ConfigOptionPoint*>(opt);
        return from_SV_check(value, &optv->value);
    } else if (def->type == coPoints) {
        ConfigOptionPoints* optv = dynamic_cast<ConfigOptionPoints*>(opt);
        std::vector<Pointf> values;
        AV* av = (AV*)SvRV(value);
        const size_t len = av_len(av)+1;
        for (size_t i = 0; i < len; i++) {
            SV** elem = av_fetch(av, i, 0);
            Pointf point;
            if (elem == NULL || !from_SV_check(*elem, &point)) return false;
            values.push_back(point);
        }
        optv->values = values;
    } else if (def->type == coBool) {
        ConfigOptionBool* optv = dynamic_cast<ConfigOptionBool*>(opt);
        optv->value = SvTRUE(value);
    } else if (def->type == coBools) {
        ConfigOptionBools* optv = dynamic_cast<ConfigOptionBools*>(opt);
        optv->values.clear();
        AV* av = (AV*)SvRV(value);
        const size_t len = av_len(av)+1;
        for (size_t i = 0; i < len; i++) {
            SV** elem = av_fetch(av, i, 0);
            if (elem == NULL) return false;
            optv->values.push_back(SvTRUE(*elem));
        }
    } else {
        if (!opt->deserialize( std::string(SvPV_nolen(value)) )) return false;
    }
    return true;
}

/* This method is implemented as a workaround for this typemap bug:
   https://rt.cpan.org/Public/Bug/Display.html?id=94110 */
bool
ConfigBase__set_deserialize(ConfigBase* THIS, const t_config_option_key &opt_key, SV* str) {
    size_t len;
    const char * c = SvPV(str, len);
    std::string value(c, len);
    
    return THIS->set_deserialize(opt_key, value);
}

void
ConfigBase__set_ifndef(ConfigBase* THIS, const t_config_option_key &opt_key, SV* value, bool deserialize)
{
    if (!THIS->has(opt_key)) {
        if (deserialize) {
            ConfigBase__set_deserialize(THIS, opt_key, value);
        } else {
            ConfigBase__set(THIS, opt_key, value);
        }
    }
}

bool
StaticConfig__set(StaticConfig* THIS, const t_config_option_key &opt_key, SV* value) {
    const ConfigOptionDef* optdef = THIS->def->get(opt_key);
    if (!optdef->shortcut.empty()) {
        for (std::vector<t_config_option_key>::const_iterator it = optdef->shortcut.begin(); it != optdef->shortcut.end(); ++it) {
            if (!StaticConfig__set(THIS, *it, value)) return false;
        }
        return true;
    }
    
    return ConfigBase__set(THIS, opt_key, value);
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
    av_store(av, 0, newSViv(THIS->x));
    av_store(av, 1, newSViv(THIS->y));
    return newRV_noinc((SV*)av);
}

void from_SV(SV* point_sv, Point* point)
{
    AV* point_av = (AV*)SvRV(point_sv);
    // get a double from Perl and round it, otherwise
    // it would get truncated
    point->x = lrint(SvNV(*av_fetch(point_av, 0, 0)));
    point->y = lrint(SvNV(*av_fetch(point_av, 1, 0)));
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

SV* to_SV_pureperl(const Pointf* point)
{
    AV* av = newAV();
    av_fill(av, 1);
    av_store(av, 0, newSVnv(point->x));
    av_store(av, 1, newSVnv(point->y));
    return newRV_noinc((SV*)av);
}

bool from_SV(SV* point_sv, Pointf* point)
{
    AV* point_av = (AV*)SvRV(point_sv);
    SV* sv_x = *av_fetch(point_av, 0, 0);
    SV* sv_y = *av_fetch(point_av, 1, 0);
    if (!looks_like_number(sv_x) || !looks_like_number(sv_y)) return false;
    
    point->x = SvNV(sv_x);
    point->y = SvNV(sv_y);
    return true;
}

bool from_SV_check(SV* point_sv, Pointf* point)
{
    if (sv_isobject(point_sv) && (SvTYPE(SvRV(point_sv)) == SVt_PVMG)) {
        if (!sv_isa(point_sv, perl_class_name(point)) && !sv_isa(point_sv, perl_class_name_ref(point)))
            CONFESS("Not a valid %s object (got %s)", perl_class_name(point), HvNAME(SvSTASH(SvRV(point_sv))));
        *point = *(Pointf*)SvIV((SV*)SvRV( point_sv ));
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

SV*
polynode_children_2_perl(const ClipperLib::PolyNode& node)
{
    AV* av = newAV();
    const int len = node.ChildCount();
    if (len > 0) av_extend(av, len-1);
    for (int i = 0; i < len; ++i) {
        av_store(av, i, polynode2perl(*node.Childs[i]));
    }
    return (SV*)newRV_noinc((SV*)av);
}

SV*
polynode2perl(const ClipperLib::PolyNode& node)
{
    HV* hv = newHV();
    Slic3r::Polygon p = ClipperPath_to_Slic3rPolygon(node.Contour);
    if (node.IsHole()) {
        (void)hv_stores( hv, "hole", Slic3r::perl_to_SV_clone_ref(p) );
    } else {
        (void)hv_stores( hv, "outer", Slic3r::perl_to_SV_clone_ref(p) );
    }
    (void)hv_stores( hv, "children", polynode_children_2_perl(node) );
    return (SV*)newRV_noinc((SV*)hv);
}

}
#endif
