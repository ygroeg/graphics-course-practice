#ifdef WIN32
#include <SDL.h>
#undef main
#else
#include <SDL2/SDL.h>
#endif

#include <GL/glew.h>

#include <chrono>
#include <iostream>
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

    uniform mat4 view;
    uniform mat4 projection;
    uniform int dimension;

    layout (location = 0) in vec2 in_position;

    out vec4 color;

    void main()
    {
        gl_Position = projection * view * vec4(in_position, 0.0, 1.0);

        if (in_position.x < dimension / 3.f)
            color = vec4(1., 1., 1., 1.);
        else
            color = vec4(1., 0., 0.,1.);
    }
)";

const char fragment_shader_source[] =
    R"(#version 330 core

    in vec4 color;

    layout (location = 0) out vec4 out_color;

    void main()
    {
        out_color = vec4(color);
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
  std::array<float, 2> position;
};

int main()
try
{
  if (SDL_Init(SDL_INIT_VIDEO) != 0)
    sdl2_fail("SDL_Init: ");

  SDL_Window *window = SDL_CreateWindow(
      "Graphics course practice 2", SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED, 800, 600,
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);

  if (!window)
    sdl2_fail("SDL_CreateWindow: ");

  int width, height;
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

  glClearColor(0.8f, 0.8f, 1.f, 0.f);

  GLuint vertex_shader = create_shader(GL_VERTEX_SHADER, vertex_shader_source);
  GLuint fragment_shader =
      create_shader(GL_FRAGMENT_SHADER, fragment_shader_source);

  GLuint program = create_program(vertex_shader, fragment_shader);
  glUseProgram(program);

  auto vertices = std::vector<vertex>{};
  auto indices = std::vector<u_int32_t>{};
  // make grid
  const auto dimension = 1024;
  const auto dimension_location = glGetUniformLocation(program, "dimension");
  auto w = dimension;
  auto h = dimension;
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++)
    {
      vertices.push_back(vertex{(float)i, (float)j});
      vertices.push_back(vertex{(float)i + 1, (float)j});
      vertices.push_back(vertex{(float)i + 1, (float)j + 1});
      vertices.push_back(vertex{(float)i, (float)j + 1});

      indices.push_back(4 * (i + j * w));
      indices.push_back(4 * (i + j * w) + 1);
      indices.push_back(4 * (i + j * w) + 2);
      indices.push_back(4 * (i + j * w));
      indices.push_back(4 * (i + j * w) + 2);
      indices.push_back(4 * (i + j * w) + 3);
    }

  auto vao = GLuint{};
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  auto vbo = GLuint{};
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(vertex),
               vertices.data(), GL_STATIC_DRAW);

  auto ebo = GLuint{};
  glGenBuffers(1, &ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               indices.size() * sizeof(decltype(indices[0])), indices.data(),
               GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vertex),
                        (void *)offsetof(vertex, position));

  float view[16] = {1, 0, 0, -500, 0, 1, 0, -500, 0, 0, 1, -5000.f, 0, 0, 0, 1};
  const auto view_location = glGetUniformLocation(program, "view");

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
          glViewport(0, 0, width, height);
          break;
        }
        break;
      case SDL_KEYDOWN:
        break;
      case SDL_KEYUP:
        break;
      }

    if (!running)
      break;

    glClear(GL_COLOR_BUFFER_BIT);

    auto near = 0.1f;
    auto far = 10000.f;
    auto fov = 120.f;
    auto right = near * tan(fov / 2);
    auto top = height * right / width;
    float projection[16] = {near / right,
                            0.f,
                            0.f,
                            0.f,
                            0.f,
                            near / top,
                            0.f,
                            0.f,
                            0.f,
                            0.f,
                            -(far + near) / (far - near),
                            -2.f * far * near / (far - near),
                            0.f,
                            0.f,
                            -1.f,
                            0.f};
    const auto projection_location =
        glGetUniformLocation(program, "projection");

    glUniform1i(dimension_location, dimension);
    glUniformMatrix4fv(view_location, 1, GL_TRUE, view);
    glUniformMatrix4fv(projection_location, 1, GL_TRUE, projection);

    glBindVertexArray(vao);
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
