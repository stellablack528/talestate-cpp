# 开发与打包

## 新主线

本地项目已自带 `.tools/python/`（含 tkinter/Tcl 和许可证），创作器入口使用它；游戏入口使用 SDK 的 Python。整体搬运不要求目标机器先安装 Python。下面开发命令的 `python` 也可换成 `.tools/python/python.exe`。重新准备环境时用 `python scripts/stage_python.py` 复制已安装的 Windows Python 标准运行环境。

Python 3.12+，运行时、创作工具均不需要 pip 依赖。Ren'Py SDK 位于 `.tools/renpy-8.6.0-sdk/`，不提交到仓库。SDK 可从 [Ren'Py 官方](https://www.renpy.org/release/8.6.0) 下载并解压到该目录。

```powershell
python scripts/run_game.py
python -m tools.world_builder.ui
```

无界面运行时：

```powershell
$env:PYTHONPATH = "$PWD/runtime"
python -m talestate worlds/lighthouse
```

模块职责：content 加载定义，engine 执行动作/任务，memory 记录 NPC 记忆，session 提供会话与存档接口，provider 调用模型，narration 显示实际事实。前端 `talestate_client.py` 只做 UI 适配与后台请求；创作工具只输出世界。

`clients/renpy/game/python-packages/talestate/`、`game/worlds/` 和 `game/fonts/` 是生成副本，不编辑、不提交。素材源在根 `worlds/`。

## 关键检查

```powershell
$env:PYTHONPATH = "$PWD/runtime"
python -m unittest discover -s tests -v
python scripts/run_game.py --lint
python scripts/run_game.py --check
```

当前六个关键流程检查覆盖路程/昼夜/睡觉跨日、AI 接取交付与一次奖励、次日赠物及自由对话、API 异常不提交、生成器共用协议、ZIP 跨目录恢复。HTTP 适配通过测试专用本地服务验证，未用真实 Key 验证第三方回复质量。--check 在真实 Ren'Py 中检查时间条与先上翻再新增文字的自动滚动；--check-save <目录> 只查看指定存档。检查使用 .deployment/renpy-check-data，不写玩家进度。

## 分发

`python scripts/run_game.py --check-save <存档目录>` 可加载现有存档检查画面，不自动执行行动。文件夹存档与专属素材的迁移见 [独立存档](SAVES.md)。

```powershell
python scripts/package_game.py
```

使用 Ren'Py 自带 distribute，产出 `dist/TaleState-0.3.0-pc.zip` 并解压到同名目录。Windows 玩家直接运行 `TaleState.exe`，不需要 Python 或 SDK。源码修改后重新打包才会进入分发包。

## 旧版

原 C++、Qt、示例、Schema、测试、发布脚本与旧文档归档在 `旧内容/原C++与Qt项目/`；旧程序在 `旧内容/历史游戏包/`，入口为 `旧内容/启动旧版.cmd`。

根 CMake 兼容入口与旧构建缓存也已归档，当前主线不依赖 CMake。确需构建旧源码，应使用归档源码作为源目录，重新配置构建目录并指定匹配的 Qt/工具链；不要直接沿用含旧绝对路径的缓存。

旧审查报告中的历史 `src/`、`apps/`、`tests/` 等路径，现对应归档源码内同名目录。完整迁移路径见 [项目修改历程与目录说明](../项目修改历程与目录说明.md) 和 [归档说明](../旧内容/README.md)。

## 项目内运行环境与存档

游戏双击入口使用自带 Ren'Py Python，创作窗口使用 `.tools/python/` 的 Python/tkinter。该环境由 `scripts/stage_python.py` 从本机标准 Python 复制，许可证随包保留；正常使用无需再次复制或安装。CLI 也可用 `.tools/python/python.exe` 代替系统 python。

现行文件夹存档、素材归属与传输约定见 [独立存档](SAVES.md)。userdata 位于项目/分发包内部，不是系统用户目录。
