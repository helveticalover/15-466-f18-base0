#include "Game.hpp"

#include "gl_errors.hpp" //helper for dumping OpenGL error messages
#include "read_chunk.hpp" //helper for reading a vector of structures from a file
#include "data_path.hpp" //helper to get paths relative to executable

#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <fstream>
#include <set>
#include <map>
#include <cstddef>
#include <random>

//helper defined later; throws if shader compilation fails:
static glm::mat4 location_v3m4(glm::vec3 v, glm::quat r);
static GLuint compile_shader(GLenum type, std::string const &source);

Game::Game() {
	{ //create an opengl program to perform sun/sky (well, directional+hemispherical) lighting:
		GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER,
			"#version 330\n"
			"uniform mat4 object_to_clip;\n"
            "uniform mat4 mv;\n"
			"uniform mat4x3 object_to_light;\n"
			"uniform mat3 normal_to_light;\n"
			"layout(location=0) in vec4 Position;\n" //note: layout keyword used to make sure that the location-0 attribute is always bound to something
			"in vec3 Normal;\n"
			"in vec4 Color;\n"
			"out vec3 position;\n"
			"out vec3 normal;\n"
			"out vec4 color;\n"
			"void main() {\n"
			"	gl_Position = object_to_clip * mv * Position;\n"
			"	position = object_to_light * Position;\n"
			"	normal = normal_to_light * Normal;\n"
			"	color = Color;\n"
			"}\n"
		);

		GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER,
			"#version 330\n"
			"uniform vec3 sun_direction;\n"
			"uniform vec3 sun_color;\n"
			"uniform vec3 sky_direction;\n"
			"uniform vec3 sky_color;\n"
			"in vec3 position;\n"
			"in vec3 normal;\n"
			"in vec4 color;\n"
			"out vec4 fragColor;\n"
			"void main() {\n"
			"	vec3 total_light = vec3(0.0, 0.0, 0.0);\n"
			"	vec3 n = normalize(normal);\n"
			"	{ //sky (hemisphere) light:\n"
			"		vec3 l = sky_direction;\n"
			"		float nl = 0.5 + 0.5 * dot(n,l);\n"
			"		total_light += nl * sky_color;\n"
			"	}\n"
			"	{ //sun (directional) light:\n"
			"		vec3 l = sun_direction;\n"
			"		float nl = max(0.0, dot(n,l));\n"
			"		total_light += nl * sun_color;\n"
			"	}\n"
			"	fragColor = vec4(color.rgb * total_light, color.a);\n"
			"}\n"
		);

		simple_shading.program = glCreateProgram();
		glAttachShader(simple_shading.program, vertex_shader);
		glAttachShader(simple_shading.program, fragment_shader);
		//shaders are reference counted so this makes sure they are freed after program is deleted:
		glDeleteShader(vertex_shader);
		glDeleteShader(fragment_shader);

		//link the shader program and throw errors if linking fails:
		glLinkProgram(simple_shading.program);
		GLint link_status = GL_FALSE;
		glGetProgramiv(simple_shading.program, GL_LINK_STATUS, &link_status);
		if (link_status != GL_TRUE) {
			std::cerr << "Failed to link shader program." << std::endl;
			GLint info_log_length = 0;
			glGetProgramiv(simple_shading.program, GL_INFO_LOG_LENGTH, &info_log_length);
			std::vector< GLchar > info_log(info_log_length, 0);
			GLsizei length = 0;
			glGetProgramInfoLog(simple_shading.program, GLsizei(info_log.size()), &length, &info_log[0]);
			std::cerr << "Info log: " << std::string(info_log.begin(), info_log.begin() + length);
			throw std::runtime_error("failed to link program");
		}

//		glEnable(GL_DEPTH_CLAMP);
//        glDisable( GL_CLIP_PLANE0, );
	}

	{ //read back uniform and attribute locations from the shader program:
		simple_shading.object_to_clip_mat4 = glGetUniformLocation(simple_shading.program, "object_to_clip");
		simple_shading.mv_mat4 = glGetUniformLocation(simple_shading.program, "mv");
		simple_shading.object_to_light_mat4x3 = glGetUniformLocation(simple_shading.program, "object_to_light");
		simple_shading.normal_to_light_mat3 = glGetUniformLocation(simple_shading.program, "normal_to_light");

		simple_shading.sun_direction_vec3 = glGetUniformLocation(simple_shading.program, "sun_direction");
		simple_shading.sun_color_vec3 = glGetUniformLocation(simple_shading.program, "sun_color");
		simple_shading.sky_direction_vec3 = glGetUniformLocation(simple_shading.program, "sky_direction");
		simple_shading.sky_color_vec3 = glGetUniformLocation(simple_shading.program, "sky_color");

		simple_shading.Position_vec4 = glGetAttribLocation(simple_shading.program, "Position");
		simple_shading.Normal_vec3 = glGetAttribLocation(simple_shading.program, "Normal");
		simple_shading.Color_vec4 = glGetAttribLocation(simple_shading.program, "Color");
	}

	struct Vertex {
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::u8vec4 Color;
	};
	static_assert(sizeof(Vertex) == 28, "Vertex should be packed.");

	{ //load mesh data from a binary blob:
		std::ifstream blob(data_path("pbj_meshes.blob"), std::ios::binary);
		//The blob will be made up of three chunks:
		// the first chunk will be vertex data (interleaved position/normal/color)
		// the second chunk will be characters
		// the third chunk will be an index, mapping a name (range of characters) to a mesh (range of vertex data)

		//read vertex data:
		std::vector< Vertex > vertices;
		read_chunk(blob, "dat0", &vertices);

		//read character data (for names):
		std::vector< char > names;
		read_chunk(blob, "str0", &names);

		//read index:
		struct IndexEntry {
			uint32_t name_begin;
			uint32_t name_end;
			uint32_t vertex_begin;
			uint32_t vertex_end;
		};
		static_assert(sizeof(IndexEntry) == 16, "IndexEntry should be packed.");

		std::vector< IndexEntry > index_entries;
		read_chunk(blob, "idx0", &index_entries);

		if (blob.peek() != EOF) {
			std::cerr << "WARNING: trailing data in meshes file." << std::endl;
		}

		//upload vertex data to the graphics card:
		glGenBuffers(1, &meshes_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, meshes_vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * vertices.size(), vertices.data(), GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		//create map to store index entries:
		std::map< std::string, Mesh > index;
		for (IndexEntry const &e : index_entries) {
			if (e.name_begin > e.name_end || e.name_end > names.size()) {
				throw std::runtime_error("invalid name indices in index.");
			}
			if (e.vertex_begin > e.vertex_end || e.vertex_end > vertices.size()) {
				throw std::runtime_error("invalid vertex indices in index.");
			}
			Mesh mesh;
			mesh.first = e.vertex_begin;
			mesh.count = e.vertex_end - e.vertex_begin;
			auto ret = index.insert(std::make_pair(
				std::string(names.begin() + e.name_begin, names.begin() + e.name_end),
				mesh));
			if (!ret.second) {
				throw std::runtime_error("duplicate name in index.");
			}
		}

		//look up into index map to extract meshes:
		auto lookup = [&index](std::string const &name) -> Mesh {
			auto f = index.find(name);
			if (f == index.end()) {
				throw std::runtime_error("Mesh named '" + name + "' does not appear in index.");
			}
			return f->second;
		};

		avatar_mesh = lookup("Avatar");
		peanut_mesh = lookup("Peanut");
		bread_mesh = lookup("Bread");
		jelly_mesh = lookup("Jelly");
		counter_mesh = lookup("Counter");
		serve_mesh = lookup("Serve");
		tile_mesh = lookup("Tile");
	}

	{ //create vertex array object to hold the map from the mesh vertex buffer to shader program attributes:
		glGenVertexArrays(1, &meshes_for_simple_shading_vao);
		glBindVertexArray(meshes_for_simple_shading_vao);
		glBindBuffer(GL_ARRAY_BUFFER, meshes_vbo);
		//note that I'm specifying a 3-vector for a 4-vector attribute here, and this is okay to do:
		// Do this for position, normal, and color vertex attributes
		glVertexAttribPointer(simple_shading.Position_vec4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLbyte *)0 + offsetof(Vertex, Position));
		glEnableVertexAttribArray(simple_shading.Position_vec4);
		if (simple_shading.Normal_vec3 != -1U) {
			glVertexAttribPointer(simple_shading.Normal_vec3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLbyte *)0 + offsetof(Vertex, Normal));
			glEnableVertexAttribArray(simple_shading.Normal_vec3);
		}
		if (simple_shading.Color_vec4 != -1U) {
			glVertexAttribPointer(simple_shading.Color_vec4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (GLbyte *)0 + offsetof(Vertex, Color));
			glEnableVertexAttribArray(simple_shading.Color_vec4);
		}
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	GL_ERRORS();

    top.is_column = 0; top.is_end = 0;
    bottom.is_column = 0; bottom.is_end = 1;
    left.is_column = 1; left.is_end = 0;
    right.is_column = 1; right.is_end = 1;

	edges = { &top, &bottom, &left, &right };
	key_meshes = { &peanut_mesh, &bread_mesh, &jelly_mesh, &serve_mesh };

	generate_level();
}

Game::~Game() {
	glDeleteVertexArrays(1, &meshes_for_simple_shading_vao);
	meshes_for_simple_shading_vao = -1U;

	glDeleteBuffers(1, &meshes_vbo);
	meshes_vbo = -1U;

	glDeleteProgram(simple_shading.program);
	simple_shading.program = -1U;

	GL_ERRORS();
}

void Game::generate_level() {
//	//set up game board with meshes and rolls:
//	board_meshes.reserve(board_size.x * board_size.y);
//	board_rotations.reserve(board_size.x * board_size.y);
//	std::mt19937 mt(0xbead1234);
//
//	// Pick random meshes with default rotations
//	for (uint32_t i = 0; i < board_size.x * board_size.y; ++i) {
//		board_meshes.emplace_back(key_meshes[mt()%key_meshes.size()]);
//		board_rotations.emplace_back(glm::quat());
//	}

	std::set< const Edge *> remaining_edges = edges;

	// Randomly place key counters on edges
	for (uint32_t i = 0; i < 4; ++i) {
		const Edge *edge = *std::next(remaining_edges.begin(), rand()%remaining_edges.size());

		uint32_t max = board_size[!(edge->is_column)];
		uint32_t placement = 1 + rand() % (max-2);
		uint32_t increment = edge->is_column ? board_size.x : 1;
		uint32_t start = edge->is_end * (edge->is_column ? board_size.x-1 : board_size.x*(board_size.y-1));

		uint32_t index = start + placement * increment;
		uint32_t x = index / board_size.x;
		uint32_t y = index % board_size.x;
		assert(x == 0 || x == board_size.x-1 || y == 0 || y == board_size.y-1);

		glm::uvec3 location = glm::uvec3(x, y, 0.0f);
		glm::uvec3 *itr = (glm::uvec3 *)(&key_locations);
		*(itr+i) = location;

		remaining_edges.erase(edge);
	}
}

// TODO
bool Game::handle_event(SDL_Event const &evt, glm::uvec2 window_size) {
	//ignore any keys that are the result of automatic key repeat:
	if (evt.type == SDL_KEYDOWN && evt.key.repeat) {
		return false;
	}
	//handle tracking the state of WSAD for roll control:
	if (evt.type == SDL_KEYDOWN || evt.type == SDL_KEYUP) {	// Press/release keys
		if (evt.key.keysym.scancode == SDL_SCANCODE_W) {
			controls.go_up = (evt.type == SDL_KEYDOWN);
			return true;
		} else if (evt.key.keysym.scancode == SDL_SCANCODE_S) {
			controls.go_down = (evt.type == SDL_KEYDOWN);
			return true;
		} else if (evt.key.keysym.scancode == SDL_SCANCODE_A) {
			controls.go_left = (evt.type == SDL_KEYDOWN);
			return true;
		} else if (evt.key.keysym.scancode == SDL_SCANCODE_D) {
			controls.go_right = (evt.type == SDL_KEYDOWN);
			return true;
		}
//		else if (evt.key.keysym.scancode == SDL_SCANCODE_SPACE) {
//		    try_pickup();
//		}
	}

	return false;
}

void Game::update(float elapsed) {
	//if the roll keys are pressed, rotate everything on the same row or column as the cursor:
//	glm::quat dr = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
//	float amt = elapsed * 1.0f;
//	if (controls.roll_left) {
//		dr = glm::angleAxis(amt, glm::vec3(0.0f, 1.0f, 0.0f)) * dr;
//	}
//	if (controls.roll_right) {
//		dr = glm::angleAxis(-amt, glm::vec3(0.0f, 1.0f, 0.0f)) * dr;
//	}
//	if (controls.roll_up) {
//		dr = glm::angleAxis(amt, glm::vec3(1.0f, 0.0f, 0.0f)) * dr;
//	}
//	if (controls.roll_down) {
//		dr = glm::angleAxis(-amt, glm::vec3(1.0f, 0.0f, 0.0f)) * dr;
//	}
//	if (dr != glm::quat()) {
//		for (uint32_t y = 0; y < board_size.y; ++y) {
//			for (uint32_t x = 0; x < board_size.x; ++x) {
//				glm::quat &r = board_rotations[y * board_size.x + x];
//				r = glm::normalize(dr * r);
//			}
//		}
//	}

    // NOTE: Based on discussion from http://www.opengl-tutorial.org/intermediate-tutorials/tutorial-17-quaternions/
    // Default orientation is (1,0,0);
	if (controls.go_left) {
		x_velocity -= elapsed * acceleration;
		avatar_rotation = glm::quat(glm::vec3(0.0f, 0.0f, glm::radians(180.0f)));
	}
	if (controls.go_right) {
		x_velocity += elapsed * acceleration;
        avatar_rotation = glm::quat();
	}
	if (controls.go_up) {
		y_velocity += elapsed * acceleration;
		avatar_rotation = glm::quat(glm::vec3(0.0f, 0.0f, glm::radians(90.0f)));
	}
	if (controls.go_down) {
        y_velocity -= elapsed * acceleration;
        avatar_rotation = glm::quat(glm::vec3(0.0f, 0.0f, glm::radians(-90.0f)));
    }

    //TODO: Add deceleration/friction
    if (!controls.go_left && !controls.go_right) {
        x_velocity = 0.0f;
    }
    if (!controls.go_up && !controls.go_down) {
        y_velocity = 0.0f;
    }

    x_velocity = glm::clamp(x_velocity, -max_velocity, max_velocity);
    y_velocity = glm::clamp(y_velocity, -max_velocity, max_velocity);
    glm::vec3 mv = x_velocity * glm::vec3(1.0f, 0.0f, 0.0f) + y_velocity  * glm::vec3(0.0f, 1.0f, 0.0f);

	if (mv != glm::vec3(0.0f, 0.0f, 0.0f)) {
		avatar_location += mv;
		avatar_location.x = std::max(1.0f, avatar_location.x);
		avatar_location.x = std::min(board_size.x-2.0f, avatar_location.x);
		avatar_location.y = std::max(1.0f, avatar_location.y);
		avatar_location.y = std::min(board_size.y-2.0f, avatar_location.y);
	}
}

void Game::draw(glm::uvec2 drawable_size) {
	//Set up a transformation matrix to fit the board in the window:
	glm::mat4 world_to_clip;
	{
		float aspect = float(drawable_size.x) / float(drawable_size.y);

		//want scale such that board * scale fits in [-aspect,aspect]x[-1.0,1.0] screen box with some leeway for shear:
		float scale = glm::min(
			1.75f * aspect / float(board_size.x),
			1.75f / float(board_size.y)
		);

		//center of board will be placed at center of screen:
		glm::vec2 center = 0.5f * glm::vec2(board_size);

		//orthogonal sheared view
        glm::mat4 shear_z = glm::mat4(
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.5f, -0.75f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
        );
//		glm::mat4 shear_y = glm::mat4(
//				1.0f, 0.0f, 0.0f, 0.0f,
//				0.5f, 1.0f, 0.0f, 0.0f,
//				0.0f, 0.0f, 1.0f, 0.0f,
//				0.0f, 0.0f, 0.0f, 1.0f
//		);

		//NOTE: glm matrices are specified in column-major order
		world_to_clip = /*shear_y */ shear_z * glm::mat4(
			scale / aspect, 0.0f, 0.0f, 0.0f,
			0.0f, scale, 0.0f, 0.0f,
			0.0f, 0.0f,-0.25f, 0.0f,
			-(scale / aspect) * center.x, -scale * center.y, 0.0f, 1.0f
		);
	}

	//set up graphics pipeline to use data from the meshes and the simple shading program:
	glBindVertexArray(meshes_for_simple_shading_vao);
	glUseProgram(simple_shading.program);

	glUniform3fv(simple_shading.sun_color_vec3, 1, glm::value_ptr(glm::vec3(0.81f, 0.81f, 0.76f)));
	glUniform3fv(simple_shading.sun_direction_vec3, 1, glm::value_ptr(glm::normalize(glm::vec3(0.2f, -0.2f, 1.0f))));
	glUniform3fv(simple_shading.sky_color_vec3, 1, glm::value_ptr(glm::vec3(0.2f, 0.2f, 0.3f)));
	glUniform3fv(simple_shading.sky_direction_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 1.0f, 0.0f)));

	//helper function to draw a given mesh with a given transformation:
	auto draw_mesh = [&](Mesh const &mesh, glm::mat4 const &object_to_world) {
		//set up the matrix uniforms:
		if (simple_shading.object_to_clip_mat4 != -1U) {
			glm::mat4 object_to_clip = world_to_clip * object_to_world;
			glUniformMatrix4fv(simple_shading.object_to_clip_mat4, 1, GL_FALSE, glm::value_ptr(object_to_clip));
		}
		if (simple_shading.object_to_light_mat4x3 != -1U) {
			glUniformMatrix4x3fv(simple_shading.object_to_light_mat4x3, 1, GL_FALSE, glm::value_ptr(object_to_world));
		}
		if (simple_shading.normal_to_light_mat3 != -1U) {
			//NOTE: if there isn't any non-uniform scaling in the object_to_world matrix, then the inverse transpose is the matrix itself, and computing it wastes some CPU time:
			glm::mat3 normal_to_world = glm::inverse(glm::transpose(glm::mat3(object_to_world)));
			glUniformMatrix3fv(simple_shading.normal_to_light_mat3, 1, GL_FALSE, glm::value_ptr(normal_to_world));
		}
        if (simple_shading.mv_mat4 != -1U) {
        	glm::mat4 mv_mat4 = projection * view * model;
            glUniformMatrix4fv(simple_shading.mv_mat4, 1, GL_FALSE, glm::value_ptr(mv_mat4));
        }

		//draw the mesh:
		glDrawArrays(GL_TRIANGLES, mesh.first, mesh.count);
	};

	for (uint32_t i = 0; i < board_size.x * board_size.y; ++i) {
		uint32_t x = i / board_size.x;
		uint32_t y = i % board_size.y;
		draw_mesh(tile_mesh, location_v3m4(glm::vec3(x, y, -0.5f), glm::quat()));

		if (on_edge(x,y) && not_occupied(x,y)) {
			draw_mesh(counter_mesh, location_v3m4(glm::vec3(x,y,0.0f), glm::quat()));
		}
	}
	draw_mesh(avatar_mesh, location_v3m4(avatar_location, avatar_rotation));
	draw_mesh(peanut_mesh, location_v3m4(key_locations.peanut, glm::quat()));
	draw_mesh(bread_mesh, location_v3m4(key_locations.bread, glm::quat()));
	draw_mesh(jelly_mesh, location_v3m4(key_locations.jelly, glm::quat()));
	draw_mesh(serve_mesh, location_v3m4(key_locations.serve, glm::quat()));

	glUseProgram(0);

	GL_ERRORS();
}

bool Game::on_edge(uint32_t x, uint32_t y) {
	return x == 0 || x == board_size.x-1 || y == 0 || y == board_size.y-1;
}

bool Game::not_occupied(uint32_t x, uint32_t y) {
	glm::uvec3 compare = glm::uvec3(x,y,0);
	return compare != key_locations.peanut && compare != key_locations.bread &&
		   compare != key_locations.jelly && compare != key_locations.serve;
}

static glm::mat4 location_v3m4(glm::vec3 v, glm::quat r) {
	return 	glm::mat4(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			v.x+0.5f, v.y+0.5f, v.z, 1.0f
	) * glm::mat4_cast(r);
}

//create and return an OpenGL vertex shader from source:
static GLuint compile_shader(GLenum type, std::string const &source) {
	GLuint shader = glCreateShader(type);
	GLchar const *str = source.c_str();
	GLint length = GLint(source.size());
	glShaderSource(shader, 1, &str, &length);
	glCompileShader(shader);
	GLint compile_status = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
	if (compile_status != GL_TRUE) {
		std::cerr << "Failed to compile shader." << std::endl;
		GLint info_log_length = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_log_length);
		std::vector< GLchar > info_log(info_log_length, 0);
		GLsizei length = 0;
		glGetShaderInfoLog(shader, GLsizei(info_log.size()), &length, &info_log[0]);
		std::cerr << "Info log: " << std::string(info_log.begin(), info_log.begin() + length);
		glDeleteShader(shader);
		throw std::runtime_error("Failed to compile shader.");
	}
	return shader;
}
