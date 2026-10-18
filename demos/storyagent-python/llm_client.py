# llm_client.py - DeepSeek 客户端
# 只负责生成剧情文字（叙事），不参与任何游戏规则判断，也不能修改玩家状态。
import os

from dotenv import load_dotenv
from openai import OpenAI

from models import PlayerState

# API 调用失败时显示的兜底文案：游戏规则引擎不依赖 LLM，可以继续运行
FALLBACK_MESSAGE = "剧情生成失败，但游戏规则引擎仍可继续运行。"


class DeepSeekClient:
    """调用 DeepSeek API 生成场景叙事文本的客户端。"""

    def __init__(self):
        # 从项目根目录的 .env 加载环境变量（API Key 不写死在代码里）
        load_dotenv()
        api_key = os.getenv("DEEPSEEK_API_KEY", "")
        self.model = os.getenv("DEEPSEEK_MODEL", "deepseek-chat")

        if api_key:
            self.client = OpenAI(api_key=api_key, base_url="https://api.deepseek.com")
        else:
            # 没有配置 Key 时置为 None，程序依然可以运行（只是没有剧情）
            self.client = None

    def generate_scene(self, player, scene, available_actions):
        """根据玩家信息和当前场景，让模型生成一段 100～200 字的中文剧情。

        参数：
            player            - PlayerState，玩家状态
            scene             - 当前场景数据（来自 story.json 的 dict）
            available_actions - 当前合法动作列表 [(action_id, 显示文本), ...]
        返回：
            一段剧情文字；生成失败时返回兜底文案。
        """
        if self.client is None:
            return FALLBACK_MESSAGE

        # 拼装发送给模型的内容：玩家名字、等级、场景名、合法动作
        action_labels = "、".join(label for _, label in available_actions)
        user_content = (
            f"玩家名字：{player.name}\n"
            f"玩家等级：{player.level}\n"
            f"当前场景：{scene['name']}\n"
            f"当前合法动作：{action_labels}\n\n"
            "请描写玩家在此场景中的所见所感。"
        )

        try:
            response = self.client.chat.completions.create(
                model=self.model,
                messages=[
                    # 系统提示词：明确限定模型的职责边界
                    {
                        "role": "system",
                        "content": (
                            "你是文字 RPG 的叙事生成器，你只能负责叙事和语言表现，"
                            "不能修改玩家属性，不能创建不存在的场景，"
                            "不能允许玩家执行规则引擎禁止的动作。"
                            "输出一段 100～200 字的中文剧情描写，"
                            "不要输出选项列表，不要输出任何规则或系统说明。"
                        ),
                    },
                    {"role": "user", "content": user_content},
                ],
                max_tokens=300,
            )
            return response.choices[0].message.content.strip()
        except Exception:
            # API 调用失败（网络错误、Key 无效等）：捕获异常，游戏继续运行
            return FALLBACK_MESSAGE
