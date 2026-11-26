define config.name = "TaleState · 自由旅途"
define config.version = "0.3.0"
define config.screen_width = 1440
define config.screen_height = 900
define config.rollback_enabled = False
define config.allow_skipping = False
define config.autosave_frequency = 0
define config.autosave_on_choice = False
define config.autosave_on_input = False
define config.save_on_mobile_background = False
define config.has_autosave = False
define config.has_quicksave = False
define config.window_title = "TaleState · 自由旅途"
define config.main_menu_music = None
define config.check_conflicting_properties = True

init -100 python:
    import os as _tale_os
    # Keep Ren'Py preferences beside the portable game data as well.
    config.savedir = _tale_os.path.join(
        _tale_os.environ.get("TALESTATE_DATA_DIR", _tale_os.path.join(config.basedir, "userdata")),
        "renpy")

define gui.text_font = "fonts/SourceHanSansLite.ttf"
define gui.interface_text_font = "fonts/SourceHanSansLite.ttf"
style default:
    font "fonts/SourceHanSansLite.ttf"
    size 22
    color "#e6dec9"

style button:
    background Solid("#3d4435")
    hover_background Solid("#566047")
    selected_background Solid("#697456")
    insensitive_background Solid("#292f27")
    padding (19, 11)

style button_text:
    size 20
    color "#e6dec9"
    hover_color "#fff0c9"
    selected_color "#fff0c9"
    insensitive_color "#777f6d"

style input:
    color "#f1e7cb"
    size 21

style frame:
    background Solid("#20261fee")
    padding (22, 20)

style vscrollbar:
    xsize 6
    unscrollable "hide"
    base_bar Solid("#2a3027")
    thumb Solid("#9d9b75")

style small_note is default:
    size 17
    color "#a6aa96"

style section_title is default:
    size 19
    color "#c5ad76"

style bar:
    ysize 16
    left_bar Solid("#b9a16b")
    right_bar Solid("#343e30")
    thumb Solid("#e4d4a5")
    thumb_offset 5

init python:
    build.name = "TaleState"
    build.directory_name = "TaleState-0.3.0"
    build.executable_name = "TaleState"
    build.classify("userdata/**", None)
    build.classify("**/__pycache__/**", None)
    build.classify("**/*.pyc", None)
    build.classify("game/cache/**", None)
    build.classify("*.log", None)
    build.classify("*.txt", None)
    build.classify("game/**.rpy", "all")
    build.classify("game/**.rpyc", "all")

init 10 python:
    if _tale_os.environ.get("TALESTATE_WINDOW_TITLE"):
        config.window_title = _tale_os.environ["TALESTATE_WINDOW_TITLE"]
