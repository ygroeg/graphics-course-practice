#ifdef WIN32
#include <SDL.h>
#undef main
#else
#include <SDL2/SDL.h>
#endif

#include <GL/glew.h>

#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace std::literals;

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

GLuint create_shader(GLenum shader_type, const char *shader_source)
{
  auto shader_id = glCreateShader(shader_type);
  glShaderSource(shader_id, 1, &shader_source, NULL);
  glCompileShader(shader_id);
  auto shader_compiled = GLint{};
  glGetShaderiv(shader_id, GL_COMPILE_STATUS, &shader_compiled);
  if (shader_compiled != GL_TRUE)
  {
    auto info_log_length = GLint{};
    glGetShaderiv(shader_id, GL_INFO_LOG_LENGTH, &info_log_length);
    auto log_length = GLsizei{};
    auto log_msg = std::string(info_log_length, '\0');
    glGetShaderInfoLog(shader_id, info_log_length, &log_length, log_msg.data());
    throw std::runtime_error(log_msg.data());
  }
  return shader_id;
}

GLuint create_program(GLuint vertex_shader, GLuint fragment_shader)
{
  auto program_id = glCreateProgram();
  glAttachShader(program_id, vertex_shader);
  glAttachShader(program_id, fragment_shader);
  glLinkProgram(program_id);

  auto program_linked = GLint{};
  glGetProgramiv(program_id, GL_LINK_STATUS, &program_linked);
  if (program_linked != GL_TRUE)
  {
    auto info_log_length = GLint{};
    glGetProgramiv(program_id, GL_INFO_LOG_LENGTH, &info_log_length);

    auto log_length = GLsizei{};
    auto log_msg = std::string(info_log_length, '\0');
    glGetProgramInfoLog(program_id, info_log_length, &log_length, log_msg.data());
    throw std::runtime_error(log_msg.data());
  }
  return program_id;
}

int main()
try
{
  if (SDL_Init(SDL_INIT_VIDEO) != 0)
    sdl2_fail("SDL_Init: ");

  SDL_Window *window = SDL_CreateWindow(
      "Graphics course practice 1", SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED, 800, 600,
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);

  if (!window)
    sdl2_fail("SDL_CreateWindow: ");

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  SDL_GLContext gl_context = SDL_GL_CreateContext(window);
  if (!gl_context)
    sdl2_fail("SDL_GL_CreateContext: ");

  if (auto result = glewInit(); result != GLEW_NO_ERROR)
    glew_fail("glewInit: ", result);

  if (!GLEW_VERSION_3_3)
    throw std::runtime_error("OpenGL 3.3 is not supported");

  glClearColor(0.8f, 0.8f, 1.f, 0.f);

  auto example = "example"sv;
  // create_shader(GL_FRAGMENT_SHADER, example.data());

  const char fragment_source[] =
      R"(#version 330 core

        layout (location = 0) out vec4 out_color;
        in vec3 color;
        void main()
        {
          // out_color = vec4(color.x, color.y, color.z, 1.0);
          // out_color = vec4(color.x, color.x, color.x, 1.0);
          // out_color = vec4(color.y, color.y, color.y, 1.0);
          if (int(floor(color.x * 10) + floor(color.y * 10)) % 2 == 0)
              out_color = vec4(1.0, 1.0, 1.0, 0.0);
          else
              out_color = vec4(0.0, 0.0, 0.0, 0.0);
        }
      )";
  auto fragment_id = create_shader(GL_FRAGMENT_SHADER, fragment_source);
  const char vertex_source[] =
      R"(#version 330 core

        const vec2 VERTICES[3] = vec2[3](
          vec2(0.0, 0.0),
          vec2(1.0, 0.0),
          vec2(0.0, 1.0)   
        );
        out vec3 color;
        void main()
        {
          gl_Position = vec4(VERTICES[gl_VertexID], 0.0, 1.0); //gl_VertexID ???
          color = vec3(VERTICES[gl_VertexID], 0.0);
        }
      )";

  auto vertex_id = create_shader(GL_VERTEX_SHADER, vertex_source);
  auto program_id = create_program(vertex_id, fragment_id);
  auto array_id = GLuint{};
  glGenVertexArrays(1, &array_id);
  // glProvokingVertex(GL_FIRST_VERTEX_CONVENTION); // ???

  bool running = true;
  while (running)
  {
    for (SDL_Event event; SDL_PollEvent(&event);)
      switch (event.type)
      {
      case SDL_QUIT:
        running = false;
        break;
      }

    if (!running)
      break;

    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program_id);
    glBindVertexArray(array_id);
    glDrawArrays(GL_TRIANGLES, 0, 3);
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
