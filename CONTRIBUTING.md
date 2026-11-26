# 参与开发

主线是 runtime/talestate、clients/renpy、tools/world_builder 和 worlds。先阅读 AGENTS.md 与 docs/architecture/world-definition-v1.md。

先做通可用的主流程，再补实际发现的问题。新增内容通过同一世界定义加载；前端不单独维护背包、任务和奖励状态。素材放在世界 media 中，通过清单引用。

基础检查：设置 PYTHONPATH=runtime，然后运行 python -m unittest discover -s tests -v。涉及界面时运行 python scripts/run_game.py --lint；交付客户端前跑一次 --check。无需为每个可逆小改动重复全部检查。

原 C++/Qt 版本在 旧内容/原C++与Qt项目，保留作参考。新功能应放入新主线。
