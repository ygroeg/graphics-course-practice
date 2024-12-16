#ifdef WIN32
#include <SDL.h>
#undef main
#else

#include <SDL2/SDL.h>

#endif

#include <GL/glew.h>

#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

#define GLM_FORCE_SWIZZLE
#define GLM_ENABLE_EXPERIMENTAL

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "obj_parser.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION

#include "gltf_loader.hpp"
#include "obj_parser.hpp"
#include "stb_image.h"
#include "tiny_obj_loader.h"

std::string to_string(std::string_view str)
{
  return std::string(str.begin(), str.end());
}

void sdl2_fail(std::string_view message)
{
  throw std::runtime_error(to_string(message) + SDL_GetError());
}

void glew_fail(std::string_view message, GLenum error)
{
  throw std::runtime_error(to_string(message) + reinterpret_cast<const char *>(
                                                    glewGetErrorString(error)));
}

const char vertex_shader_source[] =
    R"(#version 330 core

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec2 in_texoord;

out vec3 position;
out vec3 normal;
out vec2 texcoord;

void main()
{
    position = (model * vec4(in_position, 1.0)).xyz;
    gl_Position = projection * view * vec4(position, 1.0);
    normal = normalize(mat3(model) * in_normal);
    texcoord = vec2(in_texoord.x, 1 - in_texoord.y);
}
)";

const char fragment_shader_source[] =
    R"(#version 330 core

uniform vec3 camera_position;
uniform vec3 sun_direction;
uniform vec3 sun_color;
uniform sampler2D albedo_texture;
uniform mat4 model;
uniform mat4 shadow_projection_sun;
uniform sampler2D shadow_map;
uniform bool alpha;
uniform sampler2D alpha_texture;
uniform float power;
uniform float glossiness;
uniform vec3 point_light_position;
uniform vec3 point_light_attenuation;
uniform vec3 point_light_color;
uniform mat4 shadow_projection_point[6];
uniform samplerCube depthCubemap;

in vec3 position;
in vec3 normal;
in vec2 texcoord;

layout (location = 0) out vec4 out_color;

vec3 albedo;

vec3 diffuse(vec3 direction) {
    return albedo * max(0.0, dot(normal, direction));
}

vec3 specular(vec3 direction) {
    vec3 reflected_direction = 2.0 * normal * dot(normal, direction) - direction;
    vec3 view_direction = normalize(camera_position - position);
    return glossiness * albedo * pow(max(0.0, dot(reflected_direction, view_direction)), power);
}

vec3 phong(vec3 direction) {
    return diffuse(direction) + specular(direction);
}

//vec3 blur2(vec4 texcoord) {
//    vec3 sum = vec3(0.0);
//    vec3 sum_w = vec3(0.0);
//    const int N = 7;
//    float radius = 5.0;

//    for (int x = -N; x <= N; ++x) {
//        for (int y = -N; y <= N; ++y) {
//            for (int z = -N; z <= N; ++z) {
//                float c = exp(-float(x * x + y * y+ z * z) / (radius*radius*radius));
//                sum += c * texture(depthCubemap, texcoord.xyz + vec3(x,y,z) / vec3(textureSize(depthCubemap, 0).xyx)).rgb;
//                sum_w += c;
//            }
//        }
//    }
//    return sum / sum_w;
//}

vec2 blur(vec4 texcoord) {
    vec2 sum = vec2(0.0);
    vec2 sum_w = vec2(0.0);
    const int N = 1;
    float radius = 1.0;

    for (int x = -N; x <= N; ++x) {
        for (int y = -N; y <= N; ++y) {
            float c = exp(-float(x * x + y * y) / (radius*radius));
            sum += c * texture(shadow_map, texcoord.xy + vec2(x,y) / vec2(textureSize(shadow_map, 0))).rg;
            sum_w += c;
        }
    }
    return sum / sum_w;
}

float shadow_factor(mat4 projection) {
    vec4 shadow_pos = projection * vec4(position, 1.0);
    shadow_pos /= shadow_pos.w;
    shadow_pos = shadow_pos * 0.5 + vec4(0.5);
    vec2 data = blur(shadow_pos);
    float mu = data.r;
    float sigma = data.g - mu * mu;
    float z = shadow_pos.z - 0.03;
    float factor = (z < mu) ? 1.0
        : sigma / (sigma + (z - mu) * (z - mu));
    float delta = 0.125;
    return factor > delta ? (factor - delta) / (1 - delta) : 0.0;
}

vec3 calc_sun_with_shadows() {
    return sun_color * max(0.0, dot(normal, phong(sun_direction))) * shadow_factor(shadow_projection_sun);
}

//float shadow_factor2(mat4 projection) {
//    vec4 shadow_pos = projection * vec4(position, 1.0);
//    shadow_pos /= shadow_pos.w;
//    shadow_pos = shadow_pos * 0.5 + vec4(0.5);
//    vec3 data = blur2(shadow_pos);
//    float mu = data.r;
//    float sigma = data.g - mu * mu;
//    float z = shadow_pos.z - 0.03;
//    float factor = (z < mu) ? 1.0
//        : sigma / (sigma + (z - mu) * (z - mu));
//    float delta = 0.125;
//    return factor > delta ? (factor - delta) / (1 - delta) : 0.0;
//}

//vec3 calc_point_light_with_shadows() {
//    vec3 to_point = normalize(point_light_position - position);
//    float point_light_distance = distance(position, point_light_position);
//    float attenuation = point_light_attenuation.x + point_light_attenuation.y * point_light_distance + point_light_attenuation.z * point_light_distance * point_light_distance;
//    // vec3 point_light = phong(to_point) * point_light_color / attenuation;
//    vec3 point_light = vec3(0.f);
//    for (int i = 0; i < 6; i++) {
//        point_light += point_light_color * max(0.0, dot(normal, phong(to_point))) * shadow_factor2(shadow_projection_point[i]);
//    }
//    return point_light;
//    //shadow of point light
//}

void main()
{
    if(alpha && texture(alpha_texture, texcoord).x < 0.5)
        discard;

    float ambient_light = 0.25;
    albedo = texture(albedo_texture, texcoord).xyz;
    vec3 color = albedo * ambient_light;
    color += calc_sun_with_shadows();
    // color += calc_point_light_with_shadows();

    out_color = vec4(color, 1.0);
}
)";

const char rectangle_vertex_shader_source[] =
    R"(#version 330 core

const vec2 VERTICES[6] = vec2[6](
    vec2(-1.0, -1.0),
    vec2(-0.5, -1.0),
    vec2(-1.0, -0.5),
    vec2(-1.0, -0.5),
    vec2(-0.5, -1.0),
    vec2(-0.5, -0.5)
);

const vec2 TEXCOORD[6] = vec2[6](
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    vec2(0.0, 1.0),
    vec2(1.0, 0.0),
    vec2(1.0, 1.0)
);

out vec2 texcoord;

void main()
{
    texcoord = TEXCOORD[gl_VertexID];
    gl_Position = vec4(VERTICES[gl_VertexID], 0.0, 1.0);
}
)";

const char rectangle_fragment_shader_source[] =
    R"(#version 330 core

uniform sampler2D shadow_map;

in vec2 texcoord;

layout (location = 0) out vec4 out_color;

void main()
{
    out_color = vec4(texture(shadow_map, texcoord).rgb, 1.0);
}
)";

const char shadow_vertex_shader_source[] =
    R"(#version 330 core

uniform mat4 shadow_projection_sun;
uniform mat4 model;

layout (location = 0) in vec3 in_position;

void main()
{
    gl_Position = shadow_projection_sun * model * vec4(in_position, 1.0);
}
)";

const char shadow_fragment_shader_source[] =
    R"(#version 330 core

out vec4 out_vec;
void main() {
    float z = gl_FragCoord.z;
    out_vec = vec4(z, z * z + 0.25 * (dFdx(z) * dFdx(z) + dFdy(z) * dFdy(z)), 0, 0);
}
)";

const char vertex_bunny_shader_source[] =
    R"(#version 330 core

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec2 in_texcoord;

out vec3 normal;
out vec2 texcoord;

void main()
{
    gl_Position = projection * view * model * vec4(in_position, 1.0);
    normal = mat3(model) * in_normal;
    texcoord = in_texcoord;
}
)";

const char fragment_bunny_shader_source[] =
    R"(#version 330 core

uniform sampler2D albedo;
uniform vec3 light_direction;

layout (location = 0) out vec4 out_color;

in vec3 normal;
in vec2 texcoord;

void main()
{
    vec3 albedo_color = texture(albedo, texcoord).rgb;

    float ambient = 0.4;
    float diffuse = max(0.0, dot(normalize(normal), light_direction));

    out_color = vec4(albedo_color * (ambient + diffuse), 1.0);
}
)";
// const char shadow_vertex_shader_source_[] =
//     R"(#version 330 core

// uniform mat4 shadow_projection_point;
// uniform mat4 model;

// layout (location = 0) in vec3 in_position;

// void main()
//{
//     gl_Position = shadow_projection_point * model * vec4(in_position, 1.0);
// }
//)";

// const char shadow_fragment_shader_source_[] =
//     R"(#version 330 core

// out vec4 out_vec;
// void main() {
//     float z = gl_FragCoord.z;
//     out_vec = vec4(z, z * z + 0.25 * (dFdx(z) * dFdx(z) + dFdy(z) * dFdy(z)),
//     0, 0);
// }
//)";

GLuint create_shader(GLenum type, const char *source)
{
  GLuint result = glCreateShader(type);
  glShaderSource(result, 1, &source, nullptr);
  glCompileShader(result);
  GLint status;
  glGetShaderiv(result, GL_COMPILE_STATUS, &status);
  if (status != GL_TRUE)
  {
    GLint info_log_length;
    glGetShaderiv(result, GL_INFO_LOG_LENGTH, &info_log_length);
    std::string info_log(info_log_length, '\0');
    glGetShaderInfoLog(result, info_log.size(), nullptr, info_log.data());
    throw std::runtime_error("Shader compilation failed: " + info_log);
  }
  return result;
}

GLuint create_program(GLuint vertex_shader, GLuint fragment_shader)
{
  GLuint result = glCreateProgram();
  glAttachShader(result, vertex_shader);
  glAttachShader(result, fragment_shader);
  glLinkProgram(result);

  GLint status;
  glGetProgramiv(result, GL_LINK_STATUS, &status);
  if (status != GL_TRUE)
  {
    GLint info_log_length;
    glGetProgramiv(result, GL_INFO_LOG_LENGTH, &info_log_length);
    std::string info_log(info_log_length, '\0');
    glGetProgramInfoLog(result, info_log.size(), nullptr, info_log.data());
    throw std::runtime_error("Program linkage failed: " + info_log);
  }

  return result;
}

void load_scene(const auto &shapes, const auto &attrib, obj_data &scene)
{
  for (size_t s = 0; s < shapes.size(); s++)
  {
    size_t index_offset = 0;
    for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++)
    {
      size_t fv = size_t(shapes[s].mesh.num_face_vertices[f]);
      for (size_t v = 0; v < fv; v++)
      {
        tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];
        obj_data::vertex vertex;
        tinyobj::real_t vx = attrib.vertices[3 * size_t(idx.vertex_index) + 0];
        tinyobj::real_t vy = attrib.vertices[3 * size_t(idx.vertex_index) + 1];
        tinyobj::real_t vz = attrib.vertices[3 * size_t(idx.vertex_index) + 2];
        vertex.position = {vx, vy, vz};

        if (idx.normal_index >= 0)
        {
          tinyobj::real_t nx = attrib.normals[3 * size_t(idx.normal_index) + 0];
          tinyobj::real_t ny = attrib.normals[3 * size_t(idx.normal_index) + 1];
          tinyobj::real_t nz = attrib.normals[3 * size_t(idx.normal_index) + 2];
          vertex.normal = {nx, ny, nz};
        }

        if (idx.texcoord_index >= 0)
        {
          tinyobj::real_t tx =
              attrib.texcoords[2 * size_t(idx.texcoord_index) + 0];
          tinyobj::real_t ty =
              attrib.texcoords[2 * size_t(idx.texcoord_index) + 1];
          vertex.texcoord = {tx, ty};
        }
        scene.vertices.push_back(vertex);
      }
      index_offset += fv;
      shapes[s].mesh.material_ids[f];
    }
  }
}

void load_faces(const auto &shapes, std::vector<std::pair<int, int>> &faces)
{
  for (auto shape : shapes)
  {
    int cur = 0;
    int id = 0;
    for (int i = 0; i < shape.mesh.material_ids.size(); i++)
    {
      if (shape.mesh.material_ids[i] == shape.mesh.material_ids[id] && id == 0)
        cur++;
      else
      {
        if (id == 0)
          faces.emplace_back(shape.mesh.material_ids[id], 3 * cur);
        id = i;
      }
    }
    if (id == 0)
      faces.emplace_back(shape.mesh.material_ids[id],
                         shape.mesh.indices.size());
    else
      faces.emplace_back(shape.mesh.material_ids[id],
                         shape.mesh.indices.size() - 3 * cur);
  }
}

std::optional<GLuint> load_texture(const std::string &materials_dir,
                                   const std::string &name)
{
  if (name == "")
    return std::nullopt;

  int width, height, channels;
  std::string texture_path = materials_dir + name;
  std::replace(texture_path.begin(), texture_path.end(), '\\', '/');
  auto pixels = stbi_load(texture_path.data(), &width, &height, &channels, 4);
  GLuint result;
  glGenTextures(1, &result);
  glBindTexture(GL_TEXTURE_2D, result);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, pixels);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glGenerateMipmap(GL_TEXTURE_2D);

  stbi_image_free(pixels);

  return result;
}

void load_textures(const std::string &materials_dir, const auto &materials,
                   std::map<std::string, GLuint> &textures)
{
  for (const auto &material : materials)
  {
    auto tex = load_texture(materials_dir, material.ambient_texname);
    if (tex.has_value())
      textures[material.ambient_texname] = *tex;
    tex = load_texture(materials_dir, material.alpha_texname);
    if (tex.has_value())
      textures[material.alpha_texname] = *tex;
  }
}

int main()
try
{
  if (SDL_Init(SDL_INIT_VIDEO) != 0)
    sdl2_fail("SDL_Init: ");

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  SDL_Window *window = SDL_CreateWindow(
      "Sponza observer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800,
      600, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);

  if (!window)
    sdl2_fail("SDL_CreateWindow: ");

  int width, height;
  SDL_GetWindowSize(window, &width, &height);

  SDL_GLContext gl_context = SDL_GL_CreateContext(window);
  if (!gl_context)
    sdl2_fail("SDL_GL_CreateContext: ");

  if (auto result = glewInit(); result != GLEW_NO_ERROR)
    glew_fail("glewInit: ", result);

  if (!GLEW_VERSION_3_3)
    throw std::runtime_error("OpenGL 3.3 is not supported");

  glClearColor(0.8f, 0.8f, 1.f, 0.f);

  auto vertex_shader = create_shader(GL_VERTEX_SHADER, vertex_shader_source);
  auto fragment_shader =
      create_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
  auto rectangle_vertex_shader =
      create_shader(GL_VERTEX_SHADER, rectangle_vertex_shader_source);
  auto rectangle_fragment_shader =
      create_shader(GL_FRAGMENT_SHADER, rectangle_fragment_shader_source);
  auto shadow_vertex_shader =
      create_shader(GL_VERTEX_SHADER, shadow_vertex_shader_source);
  auto shadow_fragment_shader =
      create_shader(GL_FRAGMENT_SHADER, shadow_fragment_shader_source);
  auto bunny_vertex_shader =
      create_shader(GL_VERTEX_SHADER, vertex_bunny_shader_source);
  auto bunny_fragment_shader =
      create_shader(GL_FRAGMENT_SHADER, fragment_bunny_shader_source);
  //  auto shadow_vertex_shader_ =
  //      create_shader(GL_VERTEX_SHADER, shadow_vertex_shader_source_);
  //  auto shadow_fragment_shader_ =
  //      create_shader(GL_FRAGMENT_SHADER, shadow_fragment_shader_source_);
  auto program = create_program(vertex_shader, fragment_shader);
  auto rectangle_program =
      create_program(rectangle_vertex_shader, rectangle_fragment_shader);
  auto shadow_program =
      create_program(shadow_vertex_shader, shadow_fragment_shader);
  auto bunny_program =
      create_program(bunny_vertex_shader, bunny_fragment_shader);
  //  auto shadow_program_ =
  //      create_program(shadow_vertex_shader_, shadow_fragment_shader_);

  GLuint model_location = glGetUniformLocation(program, "model");
  GLuint view_location = glGetUniformLocation(program, "view");
  GLuint projection_location = glGetUniformLocation(program, "projection");
  GLuint camera_position_location =
      glGetUniformLocation(program, "camera_position");
  GLuint sun_direction_location =
      glGetUniformLocation(program, "sun_direction");
  GLuint sun_color_location = glGetUniformLocation(program, "sun_color");
  GLuint shadow_projection_sun_location_ =
      glGetUniformLocation(program, "shadow_projection_sun");
  GLuint albedo_texture_location =
      glGetUniformLocation(program, "albedo_texture");
  GLuint alpha_location = glGetUniformLocation(program, "alpha");
  GLuint alpha_texture_location =
      glGetUniformLocation(program, "alpha_texture");
  GLuint glossiness_location = glGetUniformLocation(program, "glossiness");
  GLuint power_location = glGetUniformLocation(program, "power");
  GLuint point_light_attenuation_location =
      glGetUniformLocation(program, "point_light_attenuation");
  //  GLuint point_light_position_location =
  //      glGetUniformLocation(program, "point_light_position");
  GLuint point_light_color_location =
      glGetUniformLocation(program, "point_light_color");
  GLuint shadow_map_location = glGetUniformLocation(program, "shadow_map");
  glUseProgram(program);
  glUniform1i(shadow_map_location, 0);

  GLuint rectangle_shadow_map_location =
      glGetUniformLocation(rectangle_program, "shadow_map");
  glUseProgram(rectangle_program);
  glUniform1i(rectangle_shadow_map_location, 0);

  GLuint shadow_projection_sun_location =
      glGetUniformLocation(shadow_program, "shadow_projection_sun");
  GLuint shadow_model_location = glGetUniformLocation(shadow_program, "model");

  GLuint bunny_model_location = glGetUniformLocation(bunny_program, "model");
  GLuint bunny_view_location = glGetUniformLocation(bunny_program, "view");
  GLuint bunny_projection_location =
      glGetUniformLocation(bunny_program, "projection");
  GLuint bunny_light_direction_location =
      glGetUniformLocation(bunny_program, "light_direction");
  GLuint albedo_location = glGetUniformLocation(bunny_program, "albedo");

  //  GLuint shadow_projection_point_location =
  //      glGetUniformLocation(shadow_program_, "shadow_projection_point");
  //  GLuint shadow_projection_point_location_ =
  //      glGetUniformLocation(program, "shadow_projection_point");
  //  GLuint depth_cube_location = glGetUniformLocation(program,
  //  "depthCubemap"); GLuint shadow_model_location_ =
  //      glGetUniformLocation(shadow_program_, "model");
  glUseProgram(program);
  //  glUniform1i(depth_cube_location, 1);
  glUseProgram(rectangle_program);

  std::string project_root = PROJECT_ROOT;
  std::string obj_path = project_root + "/scenes/sponza/sponza.obj";
  std::string materials_dir = project_root + "/scenes/sponza/";
  tinyobj::ObjReaderConfig reader_config;
  reader_config.mtl_search_path = materials_dir;

  tinyobj::ObjReader reader;

  if (!reader.ParseFromFile(obj_path, reader_config))
  {
    if (!reader.Error().empty())
      std::cerr << "TinyObjReader: " << reader.Error();

    exit(1);
  }

  if (!reader.Warning().empty())
    std::cout << "TinyObjReader: " << reader.Warning();

  auto &attrib = reader.GetAttrib();
  auto &shapes = reader.GetShapes();
  auto &materials = reader.GetMaterials();

  obj_data scene;
  load_scene(shapes, attrib, scene);

  const std::string model_path = project_root + "/bunny/bunny.gltf";

  auto const input_model = load_gltf(model_path);
  GLuint vbo;
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, input_model.buffer.size(),
               input_model.buffer.data(), GL_STATIC_DRAW);
  auto const &mesh = input_model.meshes[0];
  GLuint vao;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbo);

  auto setup_attribute = [](int index, gltf_model::accessor const &accessor)
  {
    glEnableVertexAttribArray(index);
    glVertexAttribPointer(index, accessor.size, accessor.type, GL_FALSE, 0,
                          reinterpret_cast<void *>(accessor.view.offset));
  };

  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  setup_attribute(0, mesh.position);
  setup_attribute(1, mesh.normal);
  setup_attribute(2, mesh.texcoord);

  GLuint texture;
  {
    auto const &mesh = input_model.meshes[0];
    auto path = std::filesystem::path(model_path).parent_path() /
                *mesh.material.texture_path;
    int width, height, channels;
    auto data = stbi_load(path.string().data(), &width, &height, &channels, 4);
    assert(data);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    stbi_image_free(data);
  }

  const float max = std::numeric_limits<decltype(max)>::max();
  auto minx = max;
  auto miny = max;
  auto minz = max;
  const float min = std::numeric_limits<decltype(min)>::min();
  auto maxx = min;
  auto maxy = min;
  auto maxz = min;
  for (const auto &vertex : scene.vertices)
  {
    minx = std::min(minx, vertex.position[0]);
    miny = std::min(miny, vertex.position[1]);
    minz = std::min(minz, vertex.position[2]);
    maxx = std::max(maxx, vertex.position[0]);
    maxy = std::max(maxy, vertex.position[1]);
    maxz = std::max(maxz, vertex.position[2]);
  }
  glm::vec3 C((minx + maxx) / 2, (miny + maxy) / 2, (minz + maxz) / 2);

  std::map<std::string, GLuint> textures;
  load_textures(materials_dir, materials, textures);

  GLuint rectangle_vao;
  glGenVertexArrays(1, &rectangle_vao);
  glBindVertexArray(rectangle_vao);

  GLuint scene_vao, scene_vbo;
  glGenVertexArrays(1, &scene_vao);
  glBindVertexArray(scene_vao);

  glGenBuffers(1, &scene_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, scene_vbo);
  glBufferData(GL_ARRAY_BUFFER,
               scene.vertices.size() * sizeof(obj_data::vertex),
               scene.vertices.data(), GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(obj_data::vertex),
                        (void *)(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(obj_data::vertex),
                        (void *)(12));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(obj_data::vertex),
                        (void *)(24));

  int shadow_map_size = 4096;

  GLuint shadow_map_texture;
  glGenTextures(1, &shadow_map_texture);
  glBindTexture(GL_TEXTURE_2D, shadow_map_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, shadow_map_size, shadow_map_size, 0,
               GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  GLuint shadow_fbo;
  glGenFramebuffers(1, &shadow_fbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, shadow_fbo);
  glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                       shadow_map_texture, 0);

  GLuint shadow_rbo;
  glGenRenderbuffers(1, &shadow_rbo);
  glBindRenderbuffer(GL_RENDERBUFFER, shadow_rbo);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, shadow_map_size,
                        shadow_map_size);
  glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, shadow_rbo);

  if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    throw std::runtime_error("Framebuffer incomplete");

  //  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

  //  GLuint depthCubemap;
  //  glGenTextures(1, &depthCubemap);
  //  glBindTexture(GL_TEXTURE_CUBE_MAP, depthCubemap);
  //  //  for (unsigned int i = 0; i < 6; ++i)
  //  //    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0,
  //  GL_DEPTH_COMPONENT,
  //  //                 shadow_map_size, shadow_map_size, 0,
  //  GL_DEPTH_COMPONENT,
  //  //                 GL_FLOAT, nullptr);
  //  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  //  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  //  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  //  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  //  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

  //  GLuint depthMapFBO;
  //  glGenFramebuffers(1, &depthMapFBO);
  //  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, depthMapFBO);
  //  glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
  //  depthCubemap,
  //                       0);
  //  glDrawBuffer(GL_NONE);
  //  glReadBuffer(GL_NONE);
  //  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    throw std::runtime_error("Framebuffer incomplete2");
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  auto last_frame_start = std::chrono::high_resolution_clock::now();

  float time = 0.f;
  std::map<SDL_Keycode, bool> button_down;

  std::vector<std::pair<int, int>> faces;
  load_faces(shapes, faces);

  auto draw_scene = [&](bool depth)
  {
    int first = 0;
    if (!depth)
    {
      glActiveTexture(GL_TEXTURE1);
      glUniform1i(albedo_texture_location, 1);
    }

    for (auto face : faces)
    {
      int id = face.first;
      if (!depth)
      {
        if (textures.contains(materials[id].ambient_texname))
          glBindTexture(GL_TEXTURE_2D, textures[materials[id].ambient_texname]);
        if (textures.contains(materials[id].alpha_texname))
        {
          glActiveTexture(GL_TEXTURE2);
          glUniform1i(alpha_texture_location, 2);
          glUniform1i(alpha_location, true);
          glBindTexture(GL_TEXTURE_2D, textures[materials[id].alpha_texname]);
          glActiveTexture(GL_TEXTURE1);
        }
        else
        {
          glUniform1i(alpha_location, false);
        }

        glUniform1f(glossiness_location, materials[id].specular[0]);
        glUniform1f(power_location, materials[id].shininess);
      }

      glDrawArrays(GL_TRIANGLES, first, face.second);

      first += face.second;
    }
    if (!depth)
      glActiveTexture(GL_TEXTURE0);
  };

  auto camera_pitch = 0.1f;
  auto camera_yaw = -0.6f;
  glm::vec3 camera_position{0.f, 1.5f, 3.f};
  //  glm::vec3 point_light_position{-1350.f, 80.f, -460.f};
  // starting position
  bool paused = false;
  bool running = true;
  while (running)
  {
    for (SDL_Event event; SDL_PollEvent(&event);)
      switch (event.type)
      {
      case SDL_QUIT:
        running = false;
        break;
      case SDL_WINDOWEVENT:
        switch (event.window.event)
        {
        case SDL_WINDOWEVENT_RESIZED:
          width = event.window.data1;
          height = event.window.data2;
          break;
        }
        break;
      case SDL_KEYDOWN:
        button_down[event.key.keysym.sym] = true;
        if (event.key.keysym.sym == SDLK_SPACE)
          paused = !paused;
        break;
      case SDL_KEYUP:
        button_down[event.key.keysym.sym] = false;
        break;
      }

    if (!running)
      break;

    auto now = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration_cast<std::chrono::duration<float>>(
                   now - last_frame_start)
                   .count();
    last_frame_start = now;
    if (!paused)
      time += dt;

    //    if (button_down[SDLK_r])
    //    {
    //      camera_pitch = -0.2f;
    //      camera_yaw = 1.8f;
    //      x = 930.f;
    //      y = -590.f;
    //      z = 140.f;
    //    }

    auto x = 0.f;
    auto y = 0.f;
    auto z = 0.f;
    auto move_delta = 300.f * dt;
    if (button_down[SDLK_w])
      x -= move_delta;
    if (button_down[SDLK_s])
      x += move_delta;
    if (button_down[SDLK_a])
      y -= move_delta;
    if (button_down[SDLK_d])
      y += move_delta;
    if (button_down[SDLK_q])
      z -= move_delta;
    if (button_down[SDLK_e])
      z += move_delta;

    auto view_delta = 3.f * dt;
    if (button_down[SDLK_LEFT])
      camera_yaw -= view_delta;
    if (button_down[SDLK_RIGHT])
      camera_yaw += view_delta;
    if (button_down[SDLK_UP])
      camera_pitch -= view_delta;
    if (button_down[SDLK_DOWN])
      camera_pitch += view_delta;

    camera_position +=
        x * glm::vec3(-std::sin(camera_yaw), 0.f, std::cos(camera_yaw));
    camera_position +=
        y * glm::vec3(std::cos(camera_yaw), 0.f, std::sin(camera_yaw));
    camera_position += z * glm::vec3(0.f, 1.f, 0.f);
    glm::mat4 view(1.f);
    view = glm::rotate(view, camera_pitch, {1.f, 0.f, 0.f});
    view = glm::rotate(view, camera_yaw, {0.f, 1.f, 0.f});
    view = glm::translate(view, -camera_position);
    glm::vec3 camera_position =
        (glm::inverse(view) * glm::vec4(0.f, 0.f, 0.f, 1.f)).xyz();
    //    if (button_down[SDLK_SPACE])
    //    {
    //      point_light_position = camera_position;
    //      std::cout << camera_position.x << '\t' << camera_position.y << '\t'
    //                << camera_position.z << '\t' << camera_pitch << '\t'
    //                << camera_yaw << std::endl;
    //    }

    glm::mat4 model(1.f);

    //    float near_plane = 1.0f;
    //    float X = 0, Y = 0, Z = 0;
    //    for (float x : {minx, maxx})
    //      for (float y : {miny, maxy})
    //        for (float z : {minz, maxz})
    //        {
    //          glm::vec3 V(x, y, z);
    //          X = std::max(X, abs(glm::dot(V - point_light_position,
    //                                       glm::vec3{1.0f, 0.0f, 0.0f})));
    //          Y = std::max(Y, abs(glm::dot(V - point_light_position,
    //                                       glm::vec3{0.0f, 1.0f, 0.0f})));
    //          Z = std::max(Z, abs(glm::dot(V - point_light_position,
    //                                       glm::vec3{0.0f, 0.0f, 1.0f})));
    //        }
    //    float far_plane = glm::length(glm::vec3{X, Y, Z});
    //    glm::mat4 shadowProj = glm::perspective(
    //        glm::radians(90.0f), (float)shadow_map_size /
    //        (float)shadow_map_size, near_plane, far_plane);
    //    std::vector<glm::mat4> shadowTransforms;
    //    shadowTransforms.push_back(
    //        shadowProj *
    //        glm::lookAt(point_light_position,
    //                    point_light_position + glm::vec3(1.0f, 0.0f, 0.0f),
    //                    glm::vec3(0.0f, -1.0f, 0.0f)));
    //    shadowTransforms.push_back(
    //        shadowProj *
    //        glm::lookAt(point_light_position,
    //                    point_light_position + glm::vec3(-1.0f, 0.0f, 0.0f),
    //                    glm::vec3(0.0f, -1.0f, 0.0f)));
    //    shadowTransforms.push_back(
    //        shadowProj *
    //        glm::lookAt(point_light_position,
    //                    point_light_position + glm::vec3(0.0f, 1.0f, 0.0f),
    //                    glm::vec3(0.0f, 0.0f, 1.0f)));
    //    shadowTransforms.push_back(
    //        shadowProj *
    //        glm::lookAt(point_light_position,
    //                    point_light_position + glm::vec3(0.0f, -1.0f, 0.0f),
    //                    glm::vec3(0.0f, 0.0f, -1.0f)));
    //    shadowTransforms.push_back(
    //        shadowProj *
    //        glm::lookAt(point_light_position,
    //                    point_light_position + glm::vec3(0.0f, 0.0f, 1.0f),
    //                    glm::vec3(0.0f, -1.0f, 0.0f)));
    //    shadowTransforms.push_back(
    //        shadowProj *
    //        glm::lookAt(point_light_position,
    //                    point_light_position + glm::vec3(0.0f, 0.0f, -1.0f),
    //                    glm::vec3(0.0f, -1.0f, 0.0f)));

    //    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, depthMapFBO);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, shadow_map_size, shadow_map_size);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    //    glUseProgram(shadow_program_);

    //    glUniformMatrix4fv(shadow_model_location_, 1, GL_FALSE,
    //                       reinterpret_cast<float *>(&model));
    //    for (unsigned int i = 0; i < 6; ++i)
    //    {
    //      glUniformMatrix4fv(shadow_projection_point_location, 1, GL_FALSE,
    //                         reinterpret_cast<float *>(&shadowTransforms[i]));
    //      glUniformMatrix4fv(shadow_projection_point_location_, 1, GL_FALSE,
    //                         reinterpret_cast<float *>(&shadowTransforms[i]));
    //    }
    glBindVertexArray(scene_vao);
    draw_scene(true);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, shadow_fbo);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, shadow_map_size, shadow_map_size);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glm::vec3 sun_direction = glm::normalize(
        glm::vec3(std::cos(time * 0.5f), 3.f, std::sin(time * 0.5f)));

    glm::vec3 light_z = -sun_direction;
    glm::vec3 light_x = glm::normalize(glm::cross(light_z, {0.f, 1.f, 0.f}));
    glm::vec3 light_y = glm::cross(light_x, light_z);

    float X = 0, Y = 0, Z = 0;
    for (float x : {minx, maxx})
      for (float y : {miny, maxy})
        for (float z : {minz, maxz})
        {
          glm::vec3 V(x, y, z);
          X = std::max(X, abs(glm::dot(V - C, light_x)));
          Y = std::max(Y, abs(glm::dot(V - C, light_y)));
          Z = std::max(Z, abs(glm::dot(V - C, light_z)));
        }

    glm::mat4 shadow_projection_sun = glm::mat4(1.f);
    shadow_projection_sun[0] = {X * light_x, 0};
    shadow_projection_sun[1] = {Y * light_y, 0};
    shadow_projection_sun[2] = {Z * light_z, 0};
    shadow_projection_sun[3] = {C, 1};
    shadow_projection_sun = glm::inverse(shadow_projection_sun);

    //    glm::mat4
    auto shadow_projection = []() {};
    glUseProgram(shadow_program);

    glUniformMatrix4fv(shadow_model_location, 1, GL_FALSE,
                       reinterpret_cast<float *>(&model));
    glUniformMatrix4fv(shadow_projection_sun_location, 1, GL_FALSE,
                       reinterpret_cast<float *>(&shadow_projection_sun));

    glBindVertexArray(scene_vao);
    draw_scene(true);

    glViewport(0, 0, width, height);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glClearColor(0.8f, 0.8f, 0.9f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    float near = 1.f;
    float far = 5000.f;
    float aspect = (float)height / (float)width;
    glm::mat4 projection =
        glm::perspective(glm::pi<float>() / 2.f, 1.f / aspect, near, far);

    //    glBindTexture(GL_TEXTURE_CUBE_MAP, depthCubemap);

    {
      glm::mat4 model(1.f);
      auto scale_factor = 150.f;
      glm::vec3 scale = glm::vec3(scale_factor, scale_factor, scale_factor);
      model = glm::scale(model, scale);
      model = glm::translate(model,
                             glm::vec3(7.f * std::sin(time * 0.5f), .3f, 0.f));
      // bunny rotation + path
      glUseProgram(bunny_program);
      glUniformMatrix4fv(bunny_model_location, 1, GL_FALSE,
                         reinterpret_cast<float *>(&model));
      glUniformMatrix4fv(bunny_view_location, 1, GL_FALSE,
                         reinterpret_cast<float *>(&view));
      glUniformMatrix4fv(bunny_projection_location, 1, GL_FALSE,
                         reinterpret_cast<float *>(&projection));
      glUniform3fv(bunny_light_direction_location, 1,
                   reinterpret_cast<float *>(&sun_direction));
      glBindTexture(GL_TEXTURE_2D, texture);
      auto const &mesh = input_model.meshes[0];
      glBindVertexArray(vao);
      glDrawElements(GL_TRIANGLES, mesh.indices.count, mesh.indices.type,
                     reinterpret_cast<void *>(mesh.indices.view.offset));
    }
    glBindTexture(GL_TEXTURE_2D, shadow_map_texture);
    glUseProgram(program);

    glUniformMatrix4fv(model_location, 1, GL_FALSE,
                       reinterpret_cast<float *>(&model));
    glUniformMatrix4fv(view_location, 1, GL_FALSE,
                       reinterpret_cast<float *>(&view));
    glUniformMatrix4fv(projection_location, 1, GL_FALSE,
                       reinterpret_cast<float *>(&projection));
    glUniformMatrix4fv(shadow_projection_sun_location_, 1, GL_FALSE,
                       reinterpret_cast<float *>(&shadow_projection_sun));
    glUniform3fv(camera_position_location, 1, (float *)(&camera_position));
    glUniform3f(sun_color_location, 0.8f, 0.8f, 0.8f);
    glUniform3fv(sun_direction_location, 1,
                 reinterpret_cast<float *>(&sun_direction));
    //    glUniform3fv(point_light_position_location, 1,
    //                 (float *)(&point_light_position));
    glUniform3f(point_light_color_location, 1., 1., 0.);
    glUniform3f(point_light_attenuation_location, 1, 0.001, 0.0001);
    glUseProgram(program);

    glBindVertexArray(scene_vao);
    draw_scene(false);

    glUseProgram(rectangle_program);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(rectangle_vao);
    glBindTexture(GL_TEXTURE_2D, shadow_map_texture);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    SDL_GL_SwapWindow(window);
  }
  SDL_GL_DeleteContext(gl_context);
  SDL_DestroyWindow(window);
}
catch (std::exception const &e)
{
  std::cerr << e.what() << std::endl;
  return EXIT_FAILURE;
}
