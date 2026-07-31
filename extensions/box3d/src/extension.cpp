#include <dmsdk/sdk.h>
#include <stdlib.h>
#include <vector>

#define LIB_NAME "Box3D"
#define MODULE_NAME "box3d"

#include <box3d/box3d.h>

// Global Box3D world pointer and debug draw configuration
static b3WorldId g_WorldId = { 0 };
static b3DebugDraw g_DebugDraw;

// Structure to hold shape information for debug rendering
typedef struct
{
    b3ShapeType type;
    b3Vec3 extents; // Half-extents for boxes/hulls
    float radius;   // Radius for spheres
} MyDebugShape;

// Structure to store rendered shape instance data per frame
typedef struct
{
    b3ShapeType type;
    b3Vec3 extents;
    float radius;
    b3WorldTransform transform;
    b3HexColor color;
} DebugShapeInstance;

// Frame-local storage for shapes to draw
static std::vector<DebugShapeInstance> g_RenderShapes;

// Box3D Debug Draw Callbacks
static void *CreateDebugShape(const b3DebugShape *debugShape, void *context)
{
    MyDebugShape *myShape = (MyDebugShape *)malloc(sizeof(MyDebugShape));
    myShape->type = debugShape->type;

    if (debugShape->type == b3_hullShape)
    {
        b3AABB aabb = debugShape->hull->aabb;
        myShape->extents.x = (aabb.upperBound.x - aabb.lowerBound.x) * 0.5f;
        myShape->extents.y = (aabb.upperBound.y - aabb.lowerBound.y) * 0.5f;
        myShape->extents.z = (aabb.upperBound.z - aabb.lowerBound.z) * 0.5f;
    }
    else if (debugShape->type == b3_sphereShape)
    {
        myShape->radius = debugShape->sphere->radius;
    }

    return (void *)myShape;
}

static void DestroyDebugShape(void *userShape, void *context)
{
    free(userShape);
}

static bool DrawShape(void* userShape, b3WorldTransform transform, b3HexColor color, void* context)
{
    MyDebugShape* myShape = (MyDebugShape*)userShape;

    DebugShapeInstance instance;
    instance.type = myShape->type;
    instance.extents = myShape->extents;
    instance.radius = myShape->radius;
    instance.transform = transform;
    instance.color = color;

    g_RenderShapes.push_back(instance);
    return true;
}

// Function to call from Lua to get the gravity
static int Lua_GetGravity(lua_State *L)
{
    if (!B3_IS_NULL(g_WorldId))
    {
        b3Vec3 g = b3World_GetGravity(g_WorldId);
        lua_newtable(L);
        lua_pushnumber(L, g.x);
        lua_setfield(L, -2, "x");
        lua_pushnumber(L, g.y);
        lua_setfield(L, -2, "y");
        lua_pushnumber(L, g.z);
        lua_setfield(L, -2, "z");
    }
    else
    {
        lua_pushnil(L);
    }
    return 1;
}

// Function to call from Lua to get collected debug shapes for rendering
static int Lua_GetDebugShapes(lua_State *L)
{
    lua_newtable(L);
    int index = 1;

    for (size_t i = 0; i < g_RenderShapes.size(); ++i)
    {
        const DebugShapeInstance& shape = g_RenderShapes[i];

        lua_newtable(L);

        // Type
        // lua_pushinteger(L, (lua_Integer)shape.type);
        // lua_setfield(L, -2, "type");

        // Type (converted to a readable string)
        const char* typeStr = "unknown";
        if (shape.type == b3_hullShape)
        {
            typeStr = "hull";
        }
        else if (shape.type == b3_sphereShape)
        {
            typeStr = "sphere";
        }
        lua_pushstring(L, typeStr);
        lua_setfield(L, -2, "type");

        // Position (p)
        b3Vec3 pos = b3ToVec3(shape.transform.p);
        lua_newtable(L);
        lua_pushnumber(L, pos.x); lua_setfield(L, -2, "x");
        lua_pushnumber(L, pos.y); lua_setfield(L, -2, "y");
        lua_pushnumber(L, pos.z); lua_setfield(L, -2, "z");
        lua_setfield(L, -2, "position");

        // Rotation Quaternion (q) -> stored as x, y, z, w equivalent format
        lua_newtable(L);
        lua_pushnumber(L, shape.transform.q.v.x); lua_setfield(L, -2, "x");
        lua_pushnumber(L, shape.transform.q.v.y); lua_setfield(L, -2, "y");
        lua_pushnumber(L, shape.transform.q.v.z); lua_setfield(L, -2, "z");
        lua_pushnumber(L, shape.transform.q.s);   lua_setfield(L, -2, "w");
        lua_setfield(L, -2, "rotation");

        // Extents or Radius
        if (shape.type == b3_hullShape)
        {
            lua_newtable(L);
            lua_pushnumber(L, shape.extents.x); lua_setfield(L, -2, "x");
            lua_pushnumber(L, shape.extents.y); lua_setfield(L, -2, "y");
            lua_pushnumber(L, shape.extents.z); lua_setfield(L, -2, "z");
            lua_setfield(L, -2, "extents");
        }
        else if (shape.type == b3_sphereShape)
        {
            lua_pushnumber(L, shape.radius);
            lua_setfield(L, -2, "radius");
        }

        // Push the Box3D color to Lua
        // lua_newtable(L);
        // Assuming b3HexColor can be unpacked or sent as an integer/hex value, 
        // or broken down into RGB components depending on Box3D's definition:
        lua_pushinteger(L, (lua_Integer)shape.color); 
        lua_setfield(L, -2, "color");

        lua_rawseti(L, -2, index++);
    }

    g_RenderShapes.clear();
    
    return 1;
}

// Function to create a box/cuboid from Lua
// Arguments from Lua: type (string: "static" or "dynamic"), x, y, z (position),
// hx, hy, hz (half-extents), rotation_deg (optional), density (optional)
static int Lua_CreateBox(lua_State *L)
{
    if (B3_IS_NULL(g_WorldId))
    {
        lua_pushnil(L);
        return 1;
    }

    const char *typeStr = luaL_checkstring(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float z = (float)luaL_checknumber(L, 4);

    float hx = (float)luaL_checknumber(L, 5); // half-width
    float hy = (float)luaL_checknumber(L, 6); // half-height
    float hz = (float)luaL_checknumber(L, 7); // half-depth

    b3BodyDef bodyDef = b3DefaultBodyDef();
    if (strcmp(typeStr, "dynamic") == 0)
    {
        bodyDef.type = b3_dynamicBody;
    }
    else
    {
        bodyDef.type = b3_staticBody;
    }

    bodyDef.position = (b3Vec3){ x, y, z };

    // Check for optional argument: Rotation angle in degrees around Z-axis
    if (lua_isnumber(L, 8))
    {
        #ifndef M_PI
        #define M_PI 3.14159265358979323846
        #endif
        float angleDeg = (float)lua_tonumber(L, 8);
        float angleRad = angleDeg * ((float)M_PI / 180.0f);
        b3Vec3 axis = { 0.f, 0.f, 1.f };
        bodyDef.rotation = b3MakeQuatFromAxisAngle(axis, angleRad);
    }

    b3BodyId bodyId = b3CreateBody(g_WorldId, &bodyDef);

    b3BoxHull boxHull = b3MakeBoxHull(hx, hy, hz);
    b3ShapeDef shapeDef = b3DefaultShapeDef();

    if (bodyDef.type == b3_dynamicBody)
    {
        // Density (defaults to 1.0 if not provided)
        shapeDef.density = lua_isnumber(L, 8) ? (float)lua_tonumber(L, 8) : 1.0f;
    }

    b3CreateHullShape(bodyId, &shapeDef, &boxHull.base);

    // Optionally return an identifier or index if needed, 
    // or just return true to indicate success
    lua_pushboolean(L, 1);
    return 1;
}

static int Lua_CreateSphere(lua_State *L)
{
    if (B3_IS_NULL(g_WorldId)) { lua_pushnil(L); return 1; }

    const char *typeStr = luaL_checkstring(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float z = (float)luaL_checknumber(L, 4);
    float radius = (float)luaL_checknumber(L, 5);

    b3BodyDef bodyDef = b3DefaultBodyDef();
    bodyDef.type = (strcmp(typeStr, "dynamic") == 0) ? b3_dynamicBody : b3_staticBody;
    bodyDef.position = (b3Vec3){ x, y, z };

    b3BodyId bodyId = b3CreateBody(g_WorldId, &bodyDef);

    b3ShapeDef shapeDef = b3DefaultShapeDef();
    if (bodyDef.type == b3_dynamicBody && lua_isnumber(L, 6))
    {
        shapeDef.density = (float)lua_tonumber(L, 6);
    }

    b3Sphere sphere = { {0.0f, 0.0f, 0.0f}, radius };
    b3CreateSphereShape(bodyId, &shapeDef, &sphere);

    lua_pushboolean(L, 1);
    return 1;
}

// Registration of module methods
static const luaL_reg Module_methods[] = {
    { "get_gravity", Lua_GetGravity },
    { "get_debug_shapes", Lua_GetDebugShapes },
    { "create_box", Lua_CreateBox },
    { "create_sphere", Lua_CreateSphere },
    { NULL, NULL }
};

// Lua initialization
static void LuaInit(lua_State *L)
{
    luaL_register(L, MODULE_NAME, Module_methods);
    lua_pop(L, 1);
}

// Extension lifecycle functions
static dmExtension::Result AppInitializeBox3D(dmExtension::AppParams *params)
{
    return dmExtension::RESULT_OK;
}

static dmExtension::Result InitializeBox3D(dmExtension::Params *params)
{
    // Setup Debug Draw structure
    g_DebugDraw = b3DefaultDebugDraw();
    g_DebugDraw.DrawShapeFcn = DrawShape;
    g_DebugDraw.drawShapes = true;
    g_DebugDraw.drawingBounds = (b3AABB) {
        (b3Vec3) { -100.0f, -100.0f, -100.0f },
        (b3Vec3) { 100.0f, 100.0f, 100.0f }
    };

    // Create Box3D world with debug callbacks
    b3WorldDef worldDef = b3DefaultWorldDef();
    worldDef.gravity = (b3Vec3) { 0.0f, -9.8f, 0.0f };
    worldDef.createDebugShape = CreateDebugShape;
    worldDef.destroyDebugShape = DestroyDebugShape;

    g_WorldId = b3CreateWorld(&worldDef);

    LuaInit(params->m_L);

    dmLogInfo("Box3D Extension Initialized (World Empty, Ready for Lua)!");

    lua_pushboolean(params->m_L, 1);
    lua_setglobal(params->m_L, "BOX3D_READY");

    return dmExtension::RESULT_OK;
}

static dmExtension::Result AppFinalizeBox3D(dmExtension::AppParams *params)
{
    return dmExtension::RESULT_OK;
}

static dmExtension::Result FinalizeBox3D(dmExtension::Params *params)
{
    // Clean up Box3D world
    if (!B3_IS_NULL(g_WorldId))
    {
        b3DestroyWorld(g_WorldId);
        g_WorldId = (b3WorldId) { 0 };
    }
    return dmExtension::RESULT_OK;
}

static dmExtension::Result OnUpdateBox3D(dmExtension::Params *params)
{
    if (!B3_IS_NULL(g_WorldId))
    {
        // Step the physics simulation
        float timeStep = 1.f / 60.f;
        int subStepCount = 4;
        b3World_Step(g_WorldId, timeStep, subStepCount);

        // Execute drawing callbacks (populate shape transforms)
        b3World_Draw(g_WorldId, &g_DebugDraw, B3_DEFAULT_MASK_BITS);
    }
    return dmExtension::RESULT_OK;
}

static void OnEventBox3D(dmExtension::Params *params, const dmExtension::Event *event)
{
    // Handle system events if needed
}

// Extension entry point
DM_DECLARE_EXTENSION(Box3D, LIB_NAME, AppInitializeBox3D, AppFinalizeBox3D, InitializeBox3D, OnUpdateBox3D, OnEventBox3D, FinalizeBox3D)
