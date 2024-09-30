#ifdef WIN32
#include <SDL.h>
#undef main
#else
#include <SDL2/SDL.h>
#endif

#include <GL/glew.h>

#include <string_view>
#include <stdexcept>
#include <iostream>
#include <chrono>
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
    throw std::runtime_error(to_string(message) + reinterpret_cast<const char *>(glewGetErrorString(error)));
}

const char vertex_shader_source[] =
    R"(#version 330 core
        uniform mat4 view;

        layout (location = 0) in vec2 in_position;
        layout (location = 1) in float in_distance;
        layout (location = 2) in vec4 in_color;

        out vec4 color;
        out float distance;

        void main()
        {
            gl_Position = view * vec4(in_position, 0.0, 1.0);
            color = in_color;
            distance = in_distance;
        }
)";

const char fragment_shader_source[] =
    R"(#version 330 core
        uniform bool dash;
        uniform float time;

        layout (location = 0) out vec4 out_color;
        
        in vec4 color;
        in float distance;

        void main()
        {
            if (dash && mod(distance + time, 40.0) >= 20.0) 
                discard;
            else 
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

struct vec2
{
    float x;
    float y;
};

float hypot(vec2 a, vec2 b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

struct vertex
{
    vec2 position;
    float distance = 0;
    std::uint8_t color[4];
};

void correct_distance(std::vector<vertex> &vertices, vertex new_vertex)
{
    if (!vertices.empty())
        new_vertex.distance = vertices.back().distance + hypot(vertices.back().position, new_vertex.position);
    vertices.push_back(new_vertex);
}

vec2 bezier(std::vector<vertex> const &vertices, float t)
{
    std::vector<vec2> points(vertices.size());

    for (std::size_t i = 0; i < vertices.size(); ++i)
        points[i] = vertices[i].position;

    // De Casteljau's algorithm
    for (std::size_t k = 0; k + 1 < vertices.size(); ++k)
    {
        for (std::size_t i = 0; i + k + 1 < vertices.size(); ++i)
        {
            points[i].x = points[i].x * (1.f - t) + points[i + 1].x * t;
            points[i].y = points[i].y * (1.f - t) + points[i + 1].y * t;
        }
    }
    return points[0];
}

std::vector<vertex> generate_bezier(const std::vector<vertex> &vertices, int quality)
{
    static const std::uint8_t colors[][4] = {{255, 255, 0, 255}, {0, 255, 255, 255}, {255, 0, 255, 255}};
    auto ret = std::vector<vertex>{};
    int count = (vertices.size() - 1) * quality;
    auto dt = 0.f;
    for (int i = 0; i <= count; i++)
    {
        auto vert = vertex{bezier(vertices, dt), {}};
        memcpy(vert.color, colors[i % 3], sizeof(vert.color));
        correct_distance(ret, vert);
        dt += 1.f / count;
    }
    return ret;
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
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    SDL_Window *window = SDL_CreateWindow("Graphics course practice 3",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          800, 600,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);

    if (!window)
        sdl2_fail("SDL_CreateWindow: ");

    int width, height;
    SDL_GetWindowSize(window, &width, &height);

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context)
        sdl2_fail("SDL_GL_CreateContext: ");

    SDL_GL_SetSwapInterval(0);

    if (auto result = glewInit(); result != GLEW_NO_ERROR)
        glew_fail("glewInit: ", result);

    if (!GLEW_VERSION_3_3)
        throw std::runtime_error("OpenGL 3.3 is not supported");

    glClearColor(0.8f, 0.8f, 1.f, 0.f);

    auto vertex_shader = create_shader(GL_VERTEX_SHADER, vertex_shader_source);
    auto fragment_shader = create_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    auto program = create_program(vertex_shader, fragment_shader);

    GLuint view_location = glGetUniformLocation(program, "view");
    GLuint dash_location = glGetUniformLocation(program, "dash");
    GLuint time_location = glGetUniformLocation(program, "time");

    auto last_frame_start = std::chrono::high_resolution_clock::now();

    float time = 0.f;

    auto vertices = std::vector<vertex>{};
    auto vbo = GLuint{};
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    auto vao = GLuint{};
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vertex), (void *)offsetof(vertex, position));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(vertex), (void *)offsetof(vertex, distance));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(vertex), (void *)offsetof(vertex, color));

    int quality = 4;
    auto vertices_bez = std::vector<vertex>{};
    auto vbo_bez = GLuint{};
    glGenBuffers(1, &vbo_bez);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_bez);
    auto vao_bez = GLuint{};
    glGenVertexArrays(1, &vao_bez);
    glBindVertexArray(vao_bez);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vertex), (void *)offsetof(vertex, position));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(vertex), (void *)offsetof(vertex, distance));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(vertex), (void *)offsetof(vertex, color));

    bool running = true;
    bool need_redraw = false;
    while (running)
    {
        static int color = 0;
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
            case SDL_MOUSEBUTTONDOWN:
            {
                if (event.button.button == SDL_BUTTON_LEFT)
                {
                    int mouse_x = event.button.x;
                    int mouse_y = event.button.y;
                    color++;
                    static const std::uint8_t colors[][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}};
                    auto vert = vertex{{(float)event.button.x, (float)event.button.y}, {}};
                    memcpy(vert.color, colors[color % 3], sizeof(vert.color));
                    correct_distance(vertices, vert);
                }
                else if (event.button.button == SDL_BUTTON_RIGHT)
                {
                    if (!vertices.empty())
                    {
                        vertices.pop_back();
                        color--;
                    }
                }
                need_redraw = true;
                break;
            }
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_LEFT)
                    quality = 1 == quality ? 1 : quality - 1;
                else if (event.key.keysym.sym == SDLK_RIGHT)
                    quality++;
                need_redraw = true;
                break;
            }

        if (!running)
            break;

        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration_cast<std::chrono::duration<float>>(now - last_frame_start).count();
        last_frame_start = now;
        time += dt;

        glClear(GL_COLOR_BUFFER_BIT);

        float view[16] =
            {2.f / width, 0, 0, -1.f,
             0, -2.f / height, 0, 1.f,
             0, 0, 1, 0,
             0, 0, 0, 1};

        glUseProgram(program);
        glUniformMatrix4fv(view_location, 1, GL_TRUE, view);
        glLineWidth(5.f);
        glPointSize(10.f);

        if (need_redraw)
        {
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(vertex), vertices.data(), GL_STATIC_DRAW);
            if (vertices.size() > 2)
            {
                vertices_bez = generate_bezier(vertices, quality);
                glBindBuffer(GL_ARRAY_BUFFER, vbo_bez);
                glBufferData(GL_ARRAY_BUFFER, vertices_bez.size() * sizeof(vertex), vertices_bez.data(), GL_STATIC_DRAW);
            }
            need_redraw = false;
        }

        glBindVertexArray(vao);
        glUniform1i(dash_location, false);
        glDrawArrays(GL_POINTS, 0, vertices.size());
        glDrawArrays(GL_LINE_STRIP, 0, vertices.size());
        if (vertices.size() > 2)
        {
            glBindVertexArray(vao_bez);
            glUniform1i(dash_location, true);
            glUniform1f(time_location, time * 50);
            glDrawArrays(GL_LINE_STRIP, 0, vertices_bez.size());
        }

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
