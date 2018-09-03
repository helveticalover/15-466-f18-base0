#pragma once

#include "GL.hpp"

#include <SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>
#include <set>

// The 'Game' struct holds all of the game-relevant state,
// and is called by the main loop.

struct Game {
	//Game creates OpenGL resources (i.e. vertex buffer objects) in its
	//constructor and frees them in its destructor.
	Game();
	~Game();

	//handle_event is called when new mouse or keyboard events are received:
	// (note that this might be many times per frame or never)
	//The function should return 'true' if it handled the event.
	bool handle_event(SDL_Event const &evt, glm::uvec2 window_size);

	//update is called at the start of a new frame, after events are handled:
	void update(float elapsed);

	//draw is called after update:
	void draw(glm::uvec2 drawable_size);

	//------- opengl resources -------

	//shader program that draws lit objects with vertex colors:
	struct {
		GLuint program = -1U; //program object

		//uniform locations:
		GLuint object_to_clip_mat4 = -1U;
        GLuint model_scale_mat4 = -1U;
		GLuint object_to_light_mat4x3 = -1U;
		GLuint normal_to_light_mat3 = -1U;
		GLuint sun_direction_vec3 = -1U;
		GLuint sun_color_vec3 = -1U;
		GLuint sky_direction_vec3 = -1U;
		GLuint sky_color_vec3 = -1U;

		//attribute locations:
		GLuint Position_vec4 = -1U;
		GLuint Normal_vec3 = -1U;
		GLuint Color_vec4 = -1U;

	} simple_shading;

	//mesh data, stored in a vertex buffer:
	GLuint meshes_vbo = -1U; //vertex buffer holding mesh data

	//The location of each mesh in the meshes vertex buffer:
	struct Mesh {
		GLint first = 0;
		GLsizei count = 0;
	};

    Mesh avatar_mesh;
    Mesh counter_mesh;
    Mesh tile_mesh;
    Mesh peanut_mesh; Mesh peanut_gray;
    Mesh bread_mesh; Mesh bread_gray;
    Mesh jelly_mesh; Mesh jelly_gray;
    Mesh serve_mesh; Mesh serve_gray;

	GLuint meshes_for_simple_shading_vao = -1U; //vertex array object that describes how to connect the meshes_vbo to the simple_shading_program

	//---- transformations -----
	// NOTE: Based on discussion from https://solarianprogrammer.com/2013/05/22/opengl-101-matrices-projection-view-model/
    glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(0.5f, 0.5f, 0.5f));

    //orthogonal sheared view
    glm::mat4 shear_z = glm::mat4(
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.5f, -0.75f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
    );

    glm::mat4 scale_z = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, 0.25f));

    //------- sound ------------
    // NOTE: Based on code from https://gist.github.com/armornick/3447121

    struct Sound {
        uint32_t wav_length;
        uint8_t *wav_buffer;
        SDL_AudioSpec wav_spec;
    };

    Sound d0;
    Sound re;
    Sound mi;
    Sound fa;
    Sound so;

    //------- text ------------

    Mesh sandwiches_made;
    Mesh num0;
    Mesh num1;
    Mesh num2;
    Mesh num3;
    Mesh num4;
    Mesh num5;
    Mesh num6;
    Mesh num7;
    Mesh num8;
    Mesh num9;
    std::vector< Mesh * > digits;

	//------- game state -------

	// avatar movement
	// NOTE: Based on discussion from http://www.cplusplus.com/forum/general/29835/
	const float max_velocity = 0.15f;
    const float acceleration = 0.75f;
    const float deceleration = 0.75f;

    glm::vec3 avatar_location = glm::vec3(4,4,0);
    glm::quat avatar_rotation = glm::quat();
	float x_velocity = 0.0f; // tiles per second
    float y_velocity = 0.0f;

    struct {
        float go_left = false;
        float go_right = false;
        float go_up = false;
        float go_down = false;
    } controls;

    // board info
    glm::uvec2 board_size = glm::uvec2(9,9);

	struct Edge {
	    uint8_t is_row = 0;
	    uint8_t is_end = 0;
	};
    Edge top;
    Edge bottom;
    Edge left;
    Edge right;
	std::set<Edge *> edges;

	struct CounterInfo{
        Mesh *active;
        Mesh *inactive;
	    glm::uvec3 location = glm::uvec3(0,0,0);
	    glm::quat rotation = glm::quat(glm::vec3(0.0f, 0.0f, glm::radians(-90.0f)));
	};
	CounterInfo peanut;
	CounterInfo bread;
	CounterInfo jelly;
	CounterInfo serve;
    std::vector< CounterInfo * >key_counters;

    // level progression
	uint8_t next_pickup = 0;
	uint32_t num_sandwiches = 0;

	std::vector< Sound * > notes;
	std::vector< CounterInfo * > level_progression;

    //------- additional functions ------------

    void generate_level();  //randomizes board
};
