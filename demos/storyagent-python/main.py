# main.py - 程序入口：控制台游戏循环
# 职责划分：
#   PlayerState  - 保存玩家状态
#   StoryEngine  - 决定所有游戏规则（Python）
#   DeepSeekClient - 只负责生成剧情文字（LLM）
from llm_client import DeepSeekClient
from models import PlayerState
from story_engine import StoryEngine


def main():
    # 初始化三大部分：玩家状态、规则引擎、LLM 客户端
    engine = StoryEngine()
    player = PlayerState(name="Stella", level=2, current_scene=engine.story["start_scene"])
    llm = DeepSeekClient()

    print("==============================")
    print("      Story Agent Demo")
    print("==============================")

    # 游戏主循环：每一轮对应"进入一个场景"
    while True:
        scene = engine.get_current_scene(player)

        # 显示玩家状态
        print()
        print(f"玩家：{player.name}")
        print(f"等级：{player.level}")
        print(f"地点：{scene['name']}")

        # LLM 只负责生成当前场景的剧情文字
        print()
        story = llm.generate_scene(player, scene, engine.get_available_actions(player))
        print(story)

        # Python 引擎负责列出当前所有合法动作
        actions = engine.get_available_actions(player)
        print()
        print("可以选择的操作：")
        for i, (_, label) in enumerate(actions, start=1):
            print(f"{i}. {label}")
        print("0. 退出游戏")

        # 读取玩家输入，把编号转换成 action_id
        choice = input("请输入编号: ").strip()

        if choice == "0":
            print("再见！")
            break

        # 输入必须是列表中的编号
        if not choice.isdigit() or not (1 <= int(choice) <= len(actions)):
            print("【输入无效】请输入上方列表中的编号。")
            continue

        action_id = actions[int(choice) - 1][0]

        # 由 Python 规则引擎校验并执行动作（LLM 不参与判断）
        success, message = engine.perform_action(player, action_id)
        if not success:
            # 校验失败：打印原因，current_scene 保持不变
            print(message)
        # 校验成功：current_scene 已被引擎修改，回到循环开头进入新场景


if __name__ == "__main__":
    main()
