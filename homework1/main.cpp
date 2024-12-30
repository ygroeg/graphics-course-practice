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
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

std::string to_string(std::string_view str) {
  return std::string(str.begin(), str.end());
}

void sdl2_fail(std::string_view message) {
  throw std::runtime_error(to_string(message) + SDL_GetError());
}

void glew_fail(std::string_view message, GLenum error) {
  throw std::runtime_error(to_string(message) + reinterpret_cast<const char *>(
                                                    glewGetErrorString(error)));
}

const char vertex_shader_source[] =
    R"(#version 330 core

    uniform mat4 model;
    uniform mat4 view;
    uniform mat4 projection;
    uniform int dimension;
    uniform int shader_variation;
    uniform float time;
    
    layout (location = 0) in vec2 in_position;

    out vec4 color;    
    
    struct MetaBall 
    {
      vec2 pos;
      float r;
      vec3 color;
    };

    const int balls_num = 10;
    MetaBall Balls[balls_num];

    void calculateBallsPositions(float t) 
    {
      float radius = dimension * 0.05;
      const float c = 1.0f;

      Balls[0].pos = 0.4 * dimension * vec2(sin(t * c), cos(t * c)) + dimension / 2.f;
      Balls[0].r = radius;
      Balls[0].color = vec3(1.0, 0.0, 0.0); 

      Balls[1].pos = 0.4 * dimension * vec2(cos(t * c), sin(t * c)) + dimension / 2.f;
      Balls[1].r = radius;
      Balls[1].color = vec3(0.0, 1.0, 0.0); 

      Balls[2].pos = 0.5 * dimension * vec2(cos(t * c), 0) + dimension / 2.f;
      Balls[2].r = radius;
      Balls[2].color = vec3(0.0, 0.0, 1.0); 

      Balls[3].pos = 0.5 * dimension * vec2(0, cos(t * c)) + dimension / 2.f;
      Balls[3].r = radius;
      Balls[3].color = vec3(0.0, 1.0, 1.0); 

      Balls[4].pos = 0.5 * dimension * vec2(sin(t * c), 0) + dimension / 2.f;
      Balls[4].r = radius;
      Balls[4].color = vec3(1.0, 0.0, 1.0); 

      Balls[5].pos = 0.5 * dimension * vec2(0, sin(t * c)) + dimension / 2.f;
      Balls[5].r = radius;
      Balls[5].color = vec3(1.0, 1.0, 0.0); 

      Balls[6].pos = 0.5 * dimension * vec2(sin(t * c), cos(t * c * 5)) + dimension / 2.f;
      Balls[6].r = radius;
      Balls[6].color = vec3(1.0, 1.0, 1.0); 

      Balls[7].pos = 0.5 * dimension * vec2(cos(t * c * 5), sin(t * c )) + dimension / 2.f;
      Balls[7].r = radius;
      Balls[7].color = vec3(1.0, 1.0, 1.0); 

      Balls[8].pos = 0.5 * dimension * vec2(sin(t * c * 5), cos(t * c)) + dimension / 2.f;
      Balls[8].r = radius;
      Balls[8].color = vec3(1.0, 1.0, 1.0); 

      Balls[9].pos = 0.5 * dimension * vec2(cos(t * c), sin(t * c * 5)) + dimension / 2.f;
      Balls[9].r = radius;
      Balls[9].color = vec3(1.0, 1.0, 1.0);
    }

    float relativeDistance(MetaBall ball, vec2 position) 
    {
        float dx = position.x - ball.pos.x;
        float dy = position.y - ball.pos.y;
        float res = (ball.r * ball.r) / (dx * dx + dy * dy);
        return res;
    }

    bool inThreshold(MetaBall ball, vec2 position) 
    {
        return relativeDistance(ball, position) > 1;
    }

    float elevationFunction(vec2 pos) 
    {
      return 1 / (2 + sin(2 * sqrt(pow(pos.x / 10, 2) + pow(pos.y / 10, 2)))) * (.75 + .5 * sin(pos.x / 10 * 2));
    }

    vec3 calculateColor(vec2 position) 
    {
      float threshold = 0.0f;
      const float line_d = 2;
      const float max_threshold = .9f;
      float max_d = 1.0 / 0.0; // infinity
      vec3 selected_color = vec3(0.0, 0.0, 0.0);
      vec3 sum = vec3(0.0, 0.0, 0.0);
      for (int i = 0; i < balls_num; i++)
      {
        switch (shader_variation)
        {
          case 0:
          case 3:
            {
              float dx = position.x - Balls[i].pos.x;
              float dy = position.y - Balls[i].pos.y;
              float d2 = dx * dx + dy * dy;
              if (d2 < max_d) 
              {
                  max_d = d2;
                  selected_color = vec3(Balls[i].color.r, Balls[i].color.g, Balls[i].color.b);
              }
              threshold += relativeDistance(Balls[i], position);
              if (threshold > max_threshold)
                  sum = selected_color;
            }
            break;
          case 1:
            {
              if (!inThreshold(Balls[i], position))
                continue;
              sum = vec3(Balls[i].color.r, Balls[i].color.g, Balls[i].color.b);
            }
            break;
          case 2:
            { 
              float dx = position.x - Balls[i].pos.x;
              float dy = position.y - Balls[i].pos.y;
              float d2 = dx * dx + dy * dy;
              float r = pow(Balls[i].r, 2);
              float dr = pow(Balls[i].r - line_d, 2);
              if (r > d2 && dr < d2) 
                sum = vec3(Balls[i].color.r, Balls[i].color.g, Balls[i].color.b);
            }
            break;
          default:
            sum = vec3(0, 0, 0);
        }
      }      
      switch (shader_variation)
      {
        case 0:
          {
            if (threshold > max_threshold)
            {
              sum = selected_color;
            }
          }
          break;
        case 3:
          {
            if (threshold > max_threshold)
            {
              sum = vec3(0.0, 1.0, 0.0);
            }
          }
          break;
        case 4:
          {
            float res = elevationFunction(position);
            sum = vec3(res, 0, 0);
          }
          break;
        default:
          break;
      }
      return sum;
    }

    void main()
    {
        calculateBallsPositions(time);
        gl_Position = projection * view * model * vec4(in_position, 0.0, 1.0);
        color = vec4(calculateColor(in_position), 1.);
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

GLuint create_shader(GLenum type, const char *source) {
  GLuint result = glCreateShader(type);
  glShaderSource(result, 1, &source, nullptr);
  glCompileShader(result);
  GLint status;
  glGetShaderiv(result, GL_COMPILE_STATUS, &status);
  if (status != GL_TRUE) {
    GLint info_log_length;
    glGetShaderiv(result, GL_INFO_LOG_LENGTH, &info_log_length);
    std::string info_log(info_log_length, '\0');
    glGetShaderInfoLog(result, info_log.size(), nullptr, info_log.data());
    throw std::runtime_error("Shader compilation failed: " + info_log);
  }
  return result;
}

GLuint create_program(GLuint vertex_shader, GLuint fragment_shader) {
  GLuint result = glCreateProgram();
  glAttachShader(result, vertex_shader);
  glAttachShader(result, fragment_shader);
  glLinkProgram(result);

  GLint status;
  glGetProgramiv(result, GL_LINK_STATUS, &status);
  if (status != GL_TRUE) {
    GLint info_log_length;
    glGetProgramiv(result, GL_INFO_LOG_LENGTH, &info_log_length);
    std::string info_log(info_log_length, '\0');
    glGetProgramInfoLog(result, info_log.size(), nullptr, info_log.data());
    throw std::runtime_error("Program linkage failed: " + info_log);
  }

  return result;
}

struct Vertex {
  std::array<float, 2> position;
};

struct Ball {
  std::array<float, 2> position;
  float radius;
};

int main() try {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) sdl2_fail("SDL_Init: ");

  SDL_Window *window = SDL_CreateWindow(
      "Graphics course practice 2", SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED, 800, 600,
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);

  if (!window) sdl2_fail("SDL_CreateWindow: ");

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

  if (!gl_context) sdl2_fail("SDL_GL_CreateContext: ");

  if (auto result = glewInit(); result != GLEW_NO_ERROR)
    glew_fail("glewInit: ", result);

  if (!GLEW_VERSION_3_3)
    throw std::runtime_error("OpenGL 3.3 is not supported");

  glClearColor(0.8f, 0.8f, 1.f, 0.f);

  GLuint vertex_shader = create_shader(GL_VERTEX_SHADER, vertex_shader_source);
  GLuint fragment_shader =
      create_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
  GLuint program = create_program(vertex_shader, fragment_shader);

  const auto model_location = glGetUniformLocation(program, "model");
  const auto view_location = glGetUniformLocation(program, "view");
  const auto projection_location = glGetUniformLocation(program, "projection");
  const auto dimension_location = glGetUniformLocation(program, "dimension");
  const auto time_location = glGetUniformLocation(program, "time");
  const auto shader_variation_location =
      glGetUniformLocation(program, "shader_variation");

  auto vertices = std::vector<Vertex>{};
  auto indices = std::vector<uint32_t>{};
  const auto dimension = 1024;

  auto w = dimension;
  auto h = dimension;
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) {
      vertices.push_back(Vertex{(float)i, (float)j});
      vertices.push_back(Vertex{(float)i + 1, (float)j});
      vertices.push_back(Vertex{(float)i + 1, (float)j + 1});
      vertices.push_back(Vertex{(float)i, (float)j + 1});

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
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
               vertices.data(), GL_STATIC_DRAW);

  auto ebo = GLuint{};
  glGenBuffers(1, &ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               indices.size() * sizeof(decltype(indices[0])), indices.data(),
               GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, position));
  auto shader_variation = 4;
  float speed = 0.0001f;
  const auto dSpeed = 0.0001f;
  bool running = true;
  bool paused = false;
  std::unordered_map<SDL_Scancode, bool> key_down;
  auto last_frame_start = std::chrono::high_resolution_clock::now();
  auto time = 0.f;
  while (running) {
    for (SDL_Event event; SDL_PollEvent(&event);) switch (event.type) {
        case SDL_QUIT:
          running = false;
          break;
        case SDL_WINDOWEVENT:
          switch (event.window.event) {
            case SDL_WINDOWEVENT_RESIZED:
              width = event.window.data1;
              height = event.window.data2;
              glViewport(0, 0, width, height);
              break;
          }
          break;
        case SDL_KEYDOWN:
          key_down[event.key.keysym.scancode] = true;
          if (event.key.keysym.sym == SDLK_SPACE) paused = !paused;
          if (key_down[SDL_SCANCODE_UP]) speed += dSpeed;
          if (key_down[SDL_SCANCODE_DOWN]) speed -= dSpeed;
          if (key_down[SDL_SCANCODE_RIGHT])
            shader_variation =
                shader_variation == 5 ? shader_variation : shader_variation + 1;
          if (key_down[SDL_SCANCODE_LEFT])
            shader_variation = shader_variation == 0 ? 0 : shader_variation - 1;

          break;
        case SDL_KEYUP:
          key_down[event.key.keysym.scancode] = false;
          break;
      }
    //    if (key_down[SDL_SCANCODE_LEFT])
    //      speed_x = -speed;
    //    else if (key_down[SDL_SCANCODE_RIGHT])
    //      speed_x = speed;
    //    else
    //      speed_x = .0f;

    if (!running) break;

    glClear(GL_COLOR_BUFFER_BIT);

    auto now = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration_cast<std::chrono::duration<float>>(
                   now - last_frame_start)
                   .count();
    if (!paused) time += speed;
    last_frame_start = now;
    auto far = 5.f;
    // float view_angle = 0.f;
    float camera_distance = 0.95f;
    float aspect = (float)height / (float)width;
    glm::mat4 model(1.f);
    glm::mat4 view(1.f);
    glm::mat4 projection = glm::ortho(-1.f, 1.f, -1.f, 1.f, -far, far);
    //    glm::mat4 projection = glm::ortho(-.5f, .5f, -.5f, .5f, -far, far);
    glm::mat4 p(0.f);
    p[0][0] = aspect;
    p[1][1] = 1.f;
    p[2][2] = -1.f;
    p[3][3] = 1.f;
    projection = projection * p;
    view = glm::mat4(1.f);
    view = glm::translate(view, {-camera_distance, -camera_distance, 1.f});
    // view = glm::rotate(view, view_angle, {1.f, 0.f, 0.f});
    auto scale_factor = 0.0018f;
    model =
        glm::scale(model, glm::vec3(scale_factor, scale_factor, scale_factor));
    glUniform1i(dimension_location, dimension);
    glUniform1i(shader_variation_location, shader_variation);
    glUniform1f(time_location, time);
    glUniformMatrix4fv(model_location, 1, GL_FALSE,
                       reinterpret_cast<float *>(&model));
    glUniformMatrix4fv(view_location, 1, GL_FALSE,
                       reinterpret_cast<float *>(&view));
    glUniformMatrix4fv(projection_location, 1, GL_FALSE,
                       reinterpret_cast<float *>(&projection));

    glUseProgram(program);
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, (void *)0);

    SDL_GL_SwapWindow(window);
  }

  SDL_GL_DeleteContext(gl_context);
  SDL_DestroyWindow(window);
} catch (std::exception const &e) {
  std::cerr << e.what() << std::endl;
  return EXIT_FAILURE;
}
