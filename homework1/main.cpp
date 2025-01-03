#ifdef WIN32
#include <SDL.h>
#undef main
#else
#include <SDL2/SDL.h>
#endif

#include <GL/glew.h>

#include <array>
#include <chrono>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

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

    uniform mat4 mvp;

    layout (location = 0) in vec3 in_position;
    layout (location = 1) in vec4 in_color;

    out vec4 color;

    void main()
    {
      gl_Position = mvp * vec4(in_position.xy, 0.0, 1.0);
        color = in_color;
      // color = vec4(1.0, 0.0, 0.0, 1.0);
      // color = vec4(gl_Position) + 0.5;
    }
)";

const char fragment_shader_source[] =
    R"(#version 330 core

    in vec4 color;

    layout (location = 0) out vec4 out_color;

    void main()
    {
      out_color = color;
    }
)";

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

struct vertex
{
  glm::vec3 position;
  std::array<std::uint8_t, 4> color;
};

struct metaball
{
  glm::vec2 position;
  glm::vec2 direction;
  float radius;
  float weight;
};

float calculate_metaball(const metaball &ball, float x, float y)
{
  float xb = ball.position.x;
  float yb = ball.position.y;
  float r = ball.radius;
  float w = ball.weight;

  return w * exp(-((x - xb) * (x - xb) + (y - yb) * (y - yb)) / (r * r));
}

int width;
int height;

glm::mat4 init_camera()
{
  const auto aspect = (float)height / (float)width;
  const auto far = 5.f;
  glm::mat4 projection = glm::ortho(-1.f, 1.f, -1.f, 1.f, -far, far);
  glm::mat4 p(0.f);
  p[0][0] = aspect;
  p[1][1] = 1.f;
  p[2][2] = -1.f;
  p[3][3] = 1.f;
  projection = projection * p;
  glm::mat4 view(1.f);
  const auto camera_distance = 0.f;
  view = glm::translate(view, {-camera_distance, -camera_distance, 0.f});
  glm::mat4 model(1.f);
  //  const auto scale_factor = 1.f;
  const auto scale_factor = 0.9f;
  //  const auto scale_factor = 0.0015f;
  // const auto scale_factor = 0.000015f;
  model = glm::scale(model, glm::vec3(scale_factor));

  return model * view * projection;
}

const auto range = 4.f;
const auto dimension = 100;
const auto w = dimension;
const auto h = dimension;
auto vertices = std::vector<vertex>{};
auto indices = std::vector<uint32_t>{};
auto metaballs = std::vector<metaball>{};

void init_metaballs()
{
  auto rd = std::random_device{};
  auto gen = std::mt19937{rd()};
  auto dis = std::uniform_real_distribution<>{-2.f, 2.f};
  auto dis2 = std::uniform_real_distribution<>{0.5f, 1.5f};

  for (int i = 0; i < 200; ++i)
    metaballs.push_back({{dis(gen), dis(gen)},
                         {dis(gen), dis(gen)},
                         (float)dis2(gen),
                         (float)dis(gen) * 0.5f});
}

void init_indicies()
{
  indices.clear();
  for (int i = 0; i < h - 1; ++i)
    for (int j = 0; j < w - 1; ++j)
    {
      indices.push_back(i * w + j + 1);
      indices.push_back(i * w + j + w);
      indices.push_back(i * w + j);
      indices.push_back(i * w + j + 1);
      indices.push_back(i * w + j + w + 1);
      indices.push_back(i * w + j + w);
    }
}

void update_metaballs(float dt)
{
  for (auto &ball : metaballs)
  {
    ball.position.x += dt * ball.direction.x;
    ball.position.y += dt * ball.direction.y;
    ball.direction.x *= abs(ball.position.x) > range ? -1 : 1;
    ball.direction.y *= abs(ball.position.y) > range ? -1 : 1;
  }
}

void update_vertices()
{
  float max_z = -1e9;
  float min_z = 1e9;
  for (int i = 0; i < dimension; ++i)
    for (int j = 0; j < dimension; ++j)
    {
      float x = 2.f * range * i / (dimension - 1) - range;
      float y = 2.f * range * j / (dimension - 1) - range;
      float z = 0;

      for (const auto &ball : metaballs)
        z += calculate_metaball(ball, x, y);

      if (z / range > max_z)
        max_z = z / range;

      if (z / range < min_z)
        min_z = z / range;

      vertices[i * dimension + j] =
          vertex{{x / range, -y / range, z / range}, {0, 0, 0, 255}};
    }

  for (int i = 0; i < dimension; ++i)
    for (int j = 0; j < dimension; ++j)
    {
      glm::vec3 position = vertices[i * dimension + j].position;
      float part = (position.z - min_z) / (max_z - min_z);

      vertices[i * dimension + j].color = {uint8_t(255 * part), uint8_t(0),
                                           uint8_t(0), 255};
    }
}

int main()
try
{
  if (SDL_Init(SDL_INIT_VIDEO) != 0)
    sdl2_fail("SDL_Init: ");

  SDL_Window *window = SDL_CreateWindow(
      "hw 1", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600,
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);

  if (!window)
    sdl2_fail("SDL_CreateWindow: ");

  SDL_GetWindowSize(window, &width, &height);

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  SDL_GLContext gl_context = SDL_GL_CreateContext(window);
  SDL_GL_SetSwapInterval(0);

  if (!gl_context)
    sdl2_fail("SDL_GL_CreateContext: ");

  if (auto result = glewInit(); result != GLEW_NO_ERROR)
    glew_fail("glewInit: ", result);

  if (!GLEW_VERSION_3_3)
    throw std::runtime_error("OpenGL 3.3 is not supported");

  GLuint vertex_shader = create_shader(GL_VERTEX_SHADER, vertex_shader_source);
  GLuint fragment_shader =
      create_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
  GLuint program = create_program(vertex_shader, fragment_shader);

  const auto mvp_location = glGetUniformLocation(program, "mvp");

  // make grid
  vertices.resize(dimension * dimension);
  for (int i = 0; i < h; i++)
    for (int j = 0; j < w; j++)
    {
      float x = 2.f * range * i / (dimension - 1) - range;
      float y = 2.f * range * j / (dimension - 1) - range;
      vertices[i * dimension + j] =
          vertex{{x / range, -y / range, 0}, {0, 0, 0, 255}};
    }

  init_metaballs();
  init_indicies();

  auto vao = GLuint{};
  glGenVertexArrays(1, &vao);
  auto vbo = GLuint{};
  glGenBuffers(1, &vbo);
  auto ebo = GLuint{};
  glGenBuffers(1, &ebo);

  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               indices.size() * sizeof(decltype(indices[0])), indices.data(),
               GL_STATIC_DRAW);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(vertex), nullptr,
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void *)0);
  glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                        sizeof(std::array<uint8_t, 4>),
                        (void *)(sizeof(glm::vec3) * vertices.size()));

  std::vector<glm::vec3> coords;
  for (auto v : vertices)
    coords.push_back(v.position);

  glBufferSubData(GL_ARRAY_BUFFER, 0, (sizeof(glm::vec3) * vertices.size()),
                  coords.data());

  const auto mvp = init_camera();

  bool running = true;
  auto last_frame_start = std::chrono::high_resolution_clock::now();
  std::unordered_map<SDL_Scancode, bool> key_down;
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
          glViewport(0, 0, width, height);
          break;
        }
        break;
      case SDL_KEYDOWN:
        key_down[event.key.keysym.scancode] = true;
        break;
      case SDL_KEYUP:
        key_down[event.key.keysym.scancode] = false;
        break;
      }
    // todo dimension +- on btn click;
    if (!running)
      break;

    auto now = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration_cast<std::chrono::duration<float>>(
                   now - last_frame_start)
                   .count();
    last_frame_start = now;

    update_vertices();
    update_metaballs(dt);
    glClearColor(0.8f, 0.8f, 1.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program);
    glUniformMatrix4fv(mvp_location, 1, GL_FALSE,
                       reinterpret_cast<const float *>(&mvp));
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindVertexArray(vao);

    std::vector<std::array<uint8_t, 4>> colors;
    for (auto v : vertices)
      colors.emplace_back(v.color);

    glBufferSubData(GL_ARRAY_BUFFER, (sizeof(glm::vec3) * vertices.size()),
                    (sizeof(std::array<uint8_t, 4>) * colors.size()),
                    colors.data());

    glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, (void *)0);

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
