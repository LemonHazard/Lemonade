#include "lighting.h"
#include "json.h"

Lighting lighting_from_json(const Json &json)
{
    Lighting lighting = default_lighting();
    lighting.sun_direction   = json["sun_direction"].vector3_or(lighting.sun_direction);
    lighting.sun_color       = json["sun_color"].vector3_or(lighting.sun_color);
    lighting.sky_color       = json["sky_color"].vector3_or(lighting.sky_color);
    lighting.horizon_color   = json["horizon_color"].vector3_or(lighting.horizon_color);
    lighting.ground_color    = json["ground_color"].vector3_or(lighting.ground_color);
    lighting.exposure        = json["exposure"].float_or(lighting.exposure);
    lighting.bloom           = json["bloom"].float_or(lighting.bloom);
    lighting.shadow_distance = json["shadow_distance"].float_or(lighting.shadow_distance);
    return lighting;
}

Json lighting_to_json(const Lighting &lighting)
{
    Json json = Json::make_object();
    json.set("sun_direction", Json::make_vector3(lighting.sun_direction));
    json.set("sun_color", Json::make_vector3(lighting.sun_color));
    json.set("sky_color", Json::make_vector3(lighting.sky_color));
    json.set("horizon_color", Json::make_vector3(lighting.horizon_color));
    json.set("ground_color", Json::make_vector3(lighting.ground_color));
    json.set("exposure", Json::make_number(lighting.exposure));
    json.set("bloom", Json::make_number(lighting.bloom));
    json.set("shadow_distance", Json::make_number(lighting.shadow_distance));
    return json;
}
