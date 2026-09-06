version 0.8.0 not yet released
========================
* made the framework compile and run on Mac OSX M1 (not using OpenGL ES 2.0 but Metal!)
* cleaned the code
* added: config.fh, a file separated from main.fh which is used for
	configurations (FH only)
* added: love_math_isConvex (FH only)
* added: love_graphics_points, love_graphics_line and love_graphics_polygon (FH only)
* added: Render To Texture (canvas) system (FH only)
* added: love_filesystem_getInfo (FH only).
* added: New particle module.
* added: New UI module.
* added: love_window_setMaxSize.
* added: New scripting language, FH.
* added: Misc math utilities.
* added: love.tween, a small tweening module (src/tween/tween.c). A tween
	carries a number or an array of them and reports back in the shape it was
	given, so a point, a colour or a rectangle is one tween rather than four.
	31 Penner easings, chained steps with per-step delays and curves, looping
	with an optional yoyo, seek, pause, and love_tween_ease() for the curve on
	its own. LOVE has no love.tween; this is CLove's.
* added: love.joystick for FH. The module registered zero functions -- a game
	could receive the press and release callbacks but never ask what was
	connected, read an axis or poll a button. Twelve bindings now, mirroring
	the Lua ones: getCount, getName, isConnected, isGamepad, isDown (by name
	or by LOVE's number), getAxis, getGamepadAxis, getAxisCount,
	getButtonCount, getBallCount, getHatCount, getHat.
* fixed: love_geometry_polygon("fill") did not fill. It handed the raw vertex
	list to GL_TRIANGLE_STRIP, which is only ever right for a triangle or a
	strip-ordered quad -- a hexagon came out with a bite taken from it and a
	twelve-sided polygon came out hollow, a ring. It ear-clips the outline now,
	the way LOVE does.
* added: love_math_triangulate(points), and math_triangulate() in the engine
	beside math_isConvex(). The editor package had sixty lines of ear clipping
	in FH because there was nothing to call.
* added: love_graphics_printf(text, x, y, limit [, align, ...]) -- wrapped,
	aligned text. graphics_Font_printf() existed as a `//TODO make me` stub
	that drew nothing, and had no binding; it is implemented for both TTF and
	bitmap fonts now, with "left", "center" and "right" ("justify" behaves as
	left until per-space stretching exists).
* added: Very powerful particle system.
* fixed: a sprite batch could not hold more than 16384 quads. The shared index
	buffer was uint16_t and a quad's first vertex is 4 * i, so from quad 16384
	on the index wrapped and every sprite past that drew some earlier sprite's
	geometry -- silently. The Lua benchmark asks for 150 000 sprites; it was
	drawing the first 16384, nine times over. The indices are 32-bit now.
* fixed: love_filesystem_read() wrote '\n' where the string terminator belongs,
	so the buffer was never terminated: every caller ran off the end of the
	allocation, and the text came back with a newline and whatever followed it
	in the heap.
* fixed: love_filesystem_setSource() passed its arguments to PHYSFS_mount() the
	wrong way round -- it mounted the *previous* source and used the new one as
	the mount point, an absolute host path -- so it never mounted what it was
	given, and could take PhysFS somewhere it aborts. It also kept the caller's
	string rather than a copy.
* fixed: love_filesystem_getSource() free()d the pointer it returned, which
	belongs either to the filesystem module or to SDL.
* fixed: love_filesystem_remove() called C's remove(), which addresses the real
	filesystem relative to the process's working directory -- a different place
	from where write() puts files -- and returned remove()'s 0-for-success
	straight back as a bool, so the answer was inverted as well. Same for
	rename(), which is now a copy and a delete inside the write directory.
* fixed: FH_GRAPHICS_CANVAS and FH_GRAPHICS_QUAD were both type id 7, so a
	canvas satisfied every "is this a quad?" check and vice versa --
	love_quad_getViewport(canvas) read four floats out of a graphics_Canvas.
* fixed: love_window_setMode() read args[0] and args[1] before looking at
	n_args, and love_window_getDisplayName() passed &args[0] to fh_optnumber(),
	which takes the array and an index.
* fixed: love_window_setTitle() kept the caller's string, which a script can
	collect the moment the call returns; love_window_getTitle() handed that
	pointer back.
* fixed: love_focus() was polled, not dispatched. It ran at the top of every
	frame and fired unconditionally, so a game got sixty identical calls a
	second. It now comes off the SDL window event, where LOVE dispatches
	love.focus -- which also means it cannot miss a transition that begins and
	ends inside one frame, and reports the change on the frame it happened
	rather than the next one.
* added: love_mousefocus(focused), the callback for the pointer entering or
	leaving the window. The engine already tracked the state
	(love_window_hasMouseFocus) but never told the game about it.
* fixed: the wav decoder rejected every valid wav file. It wrote a NUL over
	the last byte of the "RIFF" tag before comparing it to "RIFF", so the
	comparison could never succeed. Rewritten to walk the RIFF chunks, which
	also fixes files with a LIST/fact chunk before the samples (the old code
	assumed the data always began at byte 44), reads the bit depth from
	bitsPerSample rather than from the "fmt " chunk's size, no longer hands
	OpenAL a length larger than the buffer it read, and frees that buffer --
	it used to leak the whole decoded file per source.
* fixed: love_audio_isLooping() always disagreed with love_audio_setLooping().
	The setter took a const pointer, so it told OpenAL and never updated the
	field the getter reads back.
* fixed: love_graphics_newShader(vertexSource, fragmentSource) -- the form
	LOVE uses -- always failed. The test was inverted, so source that already
	was a vertex shader got passed to filesystem_read() as a filename.
* fixed: shaders can declare uniforms with LOVE's `extern` again; GLSL
	reserves the word, and nothing mapped it to `uniform`.
* fixed: love_shader_sendMatrice() accepted 2, 3 or 4 numbers for a mat2/mat3/
	mat4 (which are 4, 9 and 16), read past the end of the array and called
	the vector upload instead of the matrix one. The typed senders also read
	the float member of a value that could be carrying an integer, so passing
	`1` instead of `1.0` gave garbage.
* added: love_shader_send(shader, name, value), which dispatches on the value
	the way LOVE's Shader:send does. SKILLS.md had documented it for a while;
	it did not exist.
* fixed: love_graphics_setCanvas() with no arguments -- LOVE's way of going
	back to drawing on the screen -- was rejected by the arity check, even
	though the body already treated a non-canvas argument as "unset".
* fixed: love_mouse_isDown(), setVisible(), setX() and setY() read args[0]
	without checking n_args. love_mouse_isDown() also returned a permanent
	`false` for an unknown button name rather than an error (the engine used 0
	for both "not pressed" and "no such button"), and it now accepts LOVE's
	numbers 1..5 as well as CLove's "l"/"r"/"m"/"x1"/"x2".
* fixed: love_geometry_points() reinterpreted whatever it was handed as an
	array without checking that it was one.
* fixed: bitmap fonts drew as a solid black block. love_font_setFilter() built
	a graphics_Filter on the stack and set only minMode, magMode and
	maxAnisotropy, leaving mipmapMode and mipmapLodBias as whatever was on the
	stack; a garbage mipmapMode sent the texture down graphics_Texture_setFilter's
	mipmap branch. The image bindings had always read the current filter first.
* fixed: love_font_getWrap() broke lines in the middle of a word, wherever the
	pixel limit happened to fall, instead of at the last space. It also wrote
	each codepoint back as a single char, so anything outside ASCII came out
	mangled, and grew its buffer against the *input* length while writing up to
	two bytes a step -- with the caller's malloc(strlen(line)) one byte short of
	holding even an unwrapped copy. Both fonts share one implementation now
	(src/graphics/textwrap.c), and the bitmap version no longer writes the
	terminator through a null pointer on an empty string.
* fixed: the Lua backend did not compile. lua_mainactivity.c called SAFE_FREE,
	which does not exist (the macro is CLOVE_SAFE_FREE), so USE_LUA=ON failed
	to build. In the same function: luaL_dofile()'s return was compared against
	1, which it never is, so a game that loaded cleanly was run a second time;
	argv[2] was passed to atoi() without checking it was there; and the archive
	loop leaked every script buffer but the last, then freed that one through a
	pointer that had never been assigned when the count was zero.
* fixed: a build with both USE_FH and USE_LUA ran the Lua game to completion
	and then started the FH one as well, which reported "can't open 'main.fh'"
	and returned 1 -- so a dual-backend build always exited with an error,
	however well the game had run. main() now picks a backend from the entry
	script present (or the argument given).
* fixed: love.filesystem.setIdentity() could never succeed. After creating the
	directory under the save dir it passed the bare name to PHYSFS_setWriteDir()
	and PHYSFS_mount(), which want a real path, so every game that set an
	identity failed to boot. It also now rejects "." and paths with separators
	up front, instead of letting PhysFS answer "filename is illegal or insecure".
* fixed: the particle system leaked and could corrupt the heap.
	love_particleSystem_setBufferSize() malloc'd a new particle buffer without
	freeing the old one (200 systems resized twice leaked ~4.9 MB) and left the
	live list pointing into it, and the emission loop in update() had no
	capacity check at all, so a high emission rate with a small buffer walked
	the free pointer off the end of the allocation. Also: setTexture() never
	reached the batch that does the drawing, so it did nothing on screen; the
	spawn size was indexed with the cast on the wrong side of the
	multiplication; clone() leaked the destination's buffers and handed back a
	second script handle onto one system; setQuads() kept the caller's Quad
	pointers, so a system outlived the quads a script gave it; and an empty
	colour array made update() interpolate over SIZE_MAX.
* fixed: love_particleSystem_setSizes() was never registered, so scripts could
	not call it, and it read every size past the first from beyond the end of
	the array. love_particleSystem_setQuads()'s type check was inverted --
	it rejected exactly the arrays it should have accepted.
* added: love_particleSystem_setParticleLifetime() (it had no binding at all),
	love_particleSystem_getQuads(), and LOVE 11's emission areas --
	love_particleSystem_setEmissionArea() / getEmissionArea() with the
	"ellipse", "borderellipse" and "borderrectangle" distributions, an area
	rotation angle and directionRelativeToCenter.
* added: Lua can be enabled or disabled at compile time.
* added: Build system for web!
* added: More C flags for compilation.
* added: CLove can be compiled as shared or static library.
* added: 'set' function to batch.c. Allows for changing the structure of an entity which is part of the batch.
* added: love.system.setClipboardText(text).
* added: love_system_getClipboardText().
* added: love_system_getProcessorCount().
* added: physics. Box2D 3.1.1 is vendored under src/3rdparty/box2d and exposed
	through LOVE's love.physics API (FH only): worlds, bodies, shapes, fixtures,
	nine joint kinds, ray casts, AABB queries and begin/end collision callbacks.
	Coordinates are pixels, converted with love_physics_setMeter() (30 by
	default) exactly as in LOVE. See SKILLS.md for the API and the handful of
	places Box2D 3 forced a difference (no gear or pulley joints, callbacks are
	function names, user data is a number or a string).
* fixed: a window the user resizes now updates the 2D projection and the GL
	viewport. Only a script-driven resize did, so dragging a resizable window's
	edge left everything drawn into a corner of it at the old size.
* added: the love_resize(w, h) callback (FH only), the way LOVE's love.resize
	has it -- polling the window size cannot tell a program *that* it changed.
* the editor gained hover tips on every control (with a Tips box to turn them
	off, remembered in editor.json), hand-drawn collision meshes with draggable
	vertices, and named links between entities -- read back with scene.link(e,
	name), which hands over the entity rather than an id.
* added: love_ui_hovered(), love_ui_setWindowRect(name, x, y, w, h) and
	love_ui_bringToFront(name) (FH only). Between them a script can build a
	tooltip: ask whether the pointer is over the widget just built, then put a
	window at the cursor and keep it on top. The rect call also lifts the
	long-standing limit that a window could never be moved after microui first
	saw it.
* the editor gained multi-select (shift-click, a rubber band, and moving a
	group as one), z-order (Back / Down / Up / Front over the draw order, which
	is what the entity list already was), Box2D's collision filter as two rows
	of layer toggles, joints (distance and revolute, from a two-entity
	selection), and contact marks while Play runs that name the two entities
	that touched.
* the 2D editor became a package (opt/packages/editor) a game switches on and
	off, rather than a program of its own. editor_new/update/draw/toggle, plus
	editor_revision() -- a number that moves on every edit and on nothing else,
	so a host can tell its world is out of date without diffing the level -- and
	editor_document(), which Scene() from opt/packages/scene reads directly. The
	level being edited and the level being played are the same document in the
	same process: no file to save in between, no reload, no second window. See
	opt/examples/fh/game, which toggles it with F1.
	Its layout is now computed from the actual window rather than fixed at
	1280x760, so it fits whatever hosts it.
* added: love_ui_capturesKeyboard() (FH only) -- whether microui is taking
	keystrokes, which is what a program with bare-key shortcuts has to ask
	before acting on one.
* fixed: love_quit() returning true now aborts the quit, the way LOVE's
	love.quit does. Without it SDL_QUIT was not something a script could see,
	let alone refuse, so a tool had no way to ask "save first?" when the
	window's close button was pressed.
* the editor example grew per-entity properties: a free-form key/value map on
	each entity, authored in the inspector, typed on the way in (true/false to
	a bool, anything numeric to a number, the rest text) and read back with
	scene.props()/prop()/has_prop() from opt/packages/scene. Groups say what a
	thing is, properties say with what parameters -- which is what a game needs
	instead of scripts attached to entities. Play mode now also stamps the
	entity id into each body's and fixture's user data, so a collision callback
	can get back to the entity that caused it.
* the editor example (opt/examples/fh/editor) grew levels: every *.json beside
	the game is one, the Scene button in the toolbar switches between them and
	creates new ones, and switching with unsaved work asks first. editor.json
	holds the editor's own settings -- grid step, snapping, what the viewport
	draws, and which level to reopen -- rather than putting any of that in a
	level file. The toolbar was rebuilt on a shared column grid so its two rows
	line up.
* added: an optional decimals argument to love_ui_slider() and love_ui_number()
	(FH only), 0 to 6, so a field holding a whole number is not shown as
	"20.00". A digit count rather than a printf format, since a format string
	coming from a script is a footgun.
* added: love_filesystem_list(path), love_filesystem_getWorkingDirectory() and
	love_filesystem_getHomeDirectory() (FH only). These walk the real
	filesystem instead of PhysFS's mounted view, which is what a tool needs:
	love_filesystem_enumerate() can only ever see the game's own source and save
	directories. Used by the editor's image picker
	(opt/examples/fh/editor/browser.fh).
* fixed: love_filesystem_enumerate() took its element count from strlen() of
	the first filename and then indexed the list with it, reading well past the
	end; it also leaked the list PhysFS handed back (FH only).
* added: love_ui_mouse_over(), love_ui_popup_open(name) and
	love_ui_setWindowOpen(name, open) (FH only). Between them a game that draws
	its own viewport under the UI can tell whether a click belongs to microui,
	whether a menu is still on screen (love_ui_begin_popup() answers true on the
	frame the popup is dismissed, so it cannot say), and can re-open a window
	that microui's own close button latched shut.
* fixed: love_ui_rect, love_ui_text and every other widget, layout or draw call
	aborted the process on a microui assertion when called outside a window, a
	panel or a popup -- nothing else fills the stacks they read. They raise an
	ordinary script error now (FH only).
* fixed: check boxes all shared one widget id, because mu_checkbox() derives it
	from the address of the state it is handed and the wrapper always passed the
	same stack slot. Only the last one drawn responded to the mouse.
	love_ui_checkbox()'s id argument was accepted and ignored; it is now used,
	the same way love_ui_slider() and love_ui_number() use theirs (FH only).
* fixed: love_font_getHeight() with no argument reported an error instead of
	measuring with the default font, which love_font_getWidth(text) already did
	(FH only).
* fixed: FH could lose track of a C function once enough of them were
	registered. fh_add_c_func() stored a pointer into the c_funcs stack, which
	is one contiguous array that moves when it grows, so every earlier entry
	dangled after a reallocation; the map now holds an index. Registering the
	physics module was enough to push the table over the edge and make even
	error() "unknown".
* fixed: calling a script function from C while the VM was already running (an
	engine callback fired from inside a binding, e.g. a collision callback)
	scribbled over the running function's registers. fh_call_vm_function()
	placed the new frame at register 0 whenever the frame it nested inside was
	a C call, instead of above it.
* added: end-to-end FH test suite under tests/ with a runner; the process exit code now reflects script errors (FH only).
* added: optional callbacks (love_focus, love_quit) no longer raise an error every frame when left undefined (FH only).
* added: CLAUDE.md and SKILLS.md documentation.
* added: clip path support in vector art. The vendored nanosvg now implements
	<clipPath> (userSpaceOnUse and objectBoundingBox, intersection of nested
	<g clip-path>, forward references, clip-rule), which upstream nanosvg does
	not have. Without it any .svg produced by cairo - a PDF, EPS or AI file
	converted to SVG - rendered its gradient shapes as plain coloured
	rectangles, since cairo states such a shape as a rectangle plus a clip
	path. See CLAUDE.md before upgrading nanosvg.
* fixed: colours written as fractional percentages, e.g.
	rgb(76.861572%, 89.4104%, 41.175842%), no longer come out as flat grey in
	.svg files. That is how cairo writes every colour, so an affected drawing
	lost all of its colours at once. Fixed by updating the vendored nanosvg
	(the copy in tree dated from 2014).
* added: vector art (SVG) support. love_graphics_newImage("art.svg") loads and
	love_graphics_draw() draws it like any other image; the drawing is kept and
	re-rasterized as the image is scaled up, so it stays sharp instead of turning
	into magnified pixels. Files exported from Inkscape load as they are (text
	has to be converted to paths). New: love_image_isVector,
	love_image_getVectorScale, love_image_setVectorScale and an optional scale
	argument to love_graphics_newImage (FH only; Lua gets the loading and the
	automatic re-rasterization, without the new getters).

* fixed: love_keyboard_isDown("...") crashed the engine for any key name the
	engine doesn't know (a typo, or a key with no name in the table): the lookup
	walked the name table using the *keycode* count as its bound and strcmp'd
	hundreds of entries past its end. Unknown names now simply answer false.
* fixed: love_keyboard_setKeyRepeat() and love_keyboard_setTextInput() called
	with no argument read args[0] out of bounds while formatting their error
	message (FH only).
* fixed: the OpenGL attributes (4x multisampling, colour/depth/stencil sizes)
	were set *after* SDL_CreateWindow, so SDL threw them away - the window never
	actually got the requested pixel format. They are now set before the window is
	created, and if that pixel format can't be provided (software renderers,
	remote desktops, old drivers) CLove retries once without multisampling instead
	of failing to start; both errors now report SDL_GetError().
* fixed: the repository can be built from a fresh clone again. Unanchored or
	over-broad .gitignore rules kept vendored upstream sources out of git:
	src/3rdparty/glew/build/ (the "build/" rule, so glew's own CMake project was
	missing and cmake could not even configure) and src/3rdparty/SDL2/src/core/**
	(a bare "core" rule meant for core dumps - the Windows SDL_hid/SDL_immdevice
	files among them, which are globbed, so they went missing silently). microui
	was a git submodule with no .gitmodules entry, i.e. an empty directory after a
	clone. The rules are now anchored and narrowed, and everything under
	src/3rdparty/ is tracked as plain files.
* fixed: link error "multiple definition of 'clove_running'" with GCC >= 10 and
	any other -fno-common compiler; the two main-loop flags are now declared extern
	in utils.h and defined once in tools/utils.c.
* fixed: every love_* callback is optional again. Undefined ones (love_textinput
	above all, which SDL fires for every printable key, so pressing SPACE quit a
	game that only defined love_keypressed) used to raise
	"function '...' doesn't exist" and stop the engine; love_load, love_update,
	love_draw, love_config, the keyboard, mouse and joystick callbacks are now
	all skipped when the game doesn't define them (FH only).
* fixed: love_image_getWidth/getHeight on an image now report the size the image
	is drawn at, like love_image_getDimensions already did (they used to read the
	backing image data, which for vector art is a different size).
* fixed: graphics#setFullscreen.
* fixed: newImageFont is not broken anymore.
* fixed: ~15s freeze on macOS when closing the window (SDL 2.0.8 CoreAudio device close); resolved by updating SDL.
* fixed: double-free crash on exit when a SpriteBatch was created from an image (the batch freed the image's borrowed texture).
* fixed: love_quad_setViewport read the wrong arguments and corrupted the quad (FH only).
* fixed: love_image_setPixel/getPixel argument validation (out-of-bounds read with too few arguments) (FH only).
* fixed: love_window_setVsync ignored its argument (a pointer was passed instead of the boolean) (FH only).
* fixed: graphics getShader/getFont returned a wrapper over NULL instead of null when none was set (FH only).
* fixed: use-after-free when freeing audio sources (the OpenAL buffer was detached after the source was deleted).
* fixed: audio stream sources never actually stopped (a dead condition in the stop path).
* fixed: NULL dereference when a streamed Vorbis file fails to open, plus a leak on load failure.
* fixed: unchecked realloc of the playing-stream list.

* modify: Changed the structure of folders, enabling the usage of the framework as a library.
* modify: Removed OpenAL and replaced it with MojoAL.
* modify: updated the bundled SDL to 2.32.10 (from 2.0.8); macOS deployment target raised to 10.11.
* modify: updated the embedded FH interpreter (separate integer/float types, fused loop opcode, adaptive GC, faster small-object allocation).
* modify: modernized CMakeLists.txt (CONFIGURE_DEPENDS globs, fixed the never-built shared-library option, dl linked as a library).
* modify: at shutdown the script VM is freed before GL/audio teardown, so object destructors run against live contexts.

* removed: C++ support

version 0.7.1 22.07.2017:
========================

* internally: added Zlib
* modify: love.filesystem.read to support Physfs
* modify: love.filesystem.write to support Physfs
* modify: love.filesystem.append to support Physfs
* modify: love.filesystem.exists to support Physfs
* added: love.filesystem.setIdentity(name)
* added: conf.lua accepts t.window.identity
* added: love.filesystem.enumarate(path)
* added: love.filesystem.isDir(path)
* added: love.filesystem.mkDir(path)
* added: love.filesystem.getUsrDir()
* added: love.filesystem.unmount(path)
* added: love.filesystem.mount(path)
* added: love.system.getPowerInfo()
* added Physfs as default filesystem manager. You can still use the old one if you want
* some work on emscripten (web support)
* some work on module: love.physics. I won't finish it!
* added: love.filesystem.equals(String1, String2, length(optional))
which lets you see if two strings are equal till a certain length. By default
it takes the full length of the strings.
* added: possibility to hide and create window when you want using conf.lua
* fixed: imageData: getChannel, getPixel, now accepting: grey, grey_alpha, rgb, rgba
* added: newImageFont. BitmapFonts. You can draw fonts using images (see examples for more info)
* added: networking. TCP using IPv4 or UPv6 for UNIX (see examples)
* some work on: Android port
* added: Clove compiles with C++ for more future features
* added: *.clove.tar, see examples folder -> package.
* added: love.filesystem.require("file.lua"). Acts like require even in packaging mode

version 0.7.0 04.03.2017:
=======================

* added: native which lets you code your games in C or C++ !
* added: love.graphics.newMesh
* removed: physfs and zlib
* added: build script for unix systems
* fixed: functions which were supported due to physfs
* fixed: static linking for SDL. You got to use make install just to be sure clove can
* be run and use the folder produced for deploying.
* fixed?: fixed stream support with vorbis files
* fixed: rectangles and circles alpha when using love.graphics.setColor
* fixed: Windows support should be totally functional


version 0.6.3 29.01.2017:
========================

* added full joystick support. The API is different than Love's !
* added love.filesystem.getSaveDirectory(optional: game, optional: company)
* added batch system to fonts
* added font:getWidth
* added font:getHeight
* fixed: build system for windows
* fixed: font
* fixed: https://github.com/Murii/CLove/issues/26
* fixed: love.mouse.isDown()

version 0.6.2 12.01.2017:
========================

* added No game screen
* added love.window.getDesktopDimensions()
* added love.window.getDisplayName()
* added love.window.getDisplays()
* added love.window.hasFocus()
* added love.window.hasMouseFocus()
* added love.timer.sleep(seconds)
* added love.window.setIcon(imageData)
* added love.window.getIcon(),it does not behave like Love.Clove returns the name of the file
* added pixels = ImageData:getString()
* added love.filesystem.load(fileName) see: https://love2d.org/wiki
* internal: added more functions to image_ImageData
* fixed: build system for physfs on linux

