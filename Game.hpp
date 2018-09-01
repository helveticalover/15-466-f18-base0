#pragma once

#include "GL.hpp"

#include <SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

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

	// TODO
//	Mesh tile_mesh;
//	Mesh cursor_mesh;
//	Mesh doll_mesh;
//	Mesh egg_mesh;
//	Mesh cube_mesh;

    Mesh avatar_mesh;
    Mesh peanut_mesh;
    Mesh bread_mesh;
    Mesh jelly_mesh;
    Mesh counter_mesh;
    Mesh serve_mesh;
    Mesh tile_mesh;

	GLuint meshes_for_simple_shading_vao = -1U; //vertex array object that describes how to connect the meshes_vbo to the simple_shading_program

	//------- game state -------

	glm::uvec2 board_size = glm::uvec2(4,4);
	glm::uvec2 avatar_loc = glm::vec2(2,2);

	struct {
		glm::uvec2 peanut_loc = glm::vec2(0,0);
		glm::uvec2 bread_loc = glm::vec2(0,0);
		glm::uvec2 jelly_loc = glm::vec2(0,0);
	} locations;

	struct {
		bool peanut_pickup = false;
		bool bread_pickup = false;
		bool jelly_pickup = false;
	} progress;

//		struct {
//		bool go_left = false;
//		bool go_right = false;
//		bool go_up = false;
//		bool go_down = false;
//	} controls;

	//------- level generation ------

	uint32_t levels_passed = 0;
//	bool generate_level(/*glm::uvec2 board_size*/);

	//------- base code -------------

	std::vector< Mesh const * > board_meshes;
	std::vector< glm::quat > board_rotations;

	//	glm::uvec2 cursor = glm::vec2(0,0);

	struct {
		bool roll_left = false;
		bool roll_right = false;
		bool roll_up = false;
		bool roll_down = false;
	} controls;

};
