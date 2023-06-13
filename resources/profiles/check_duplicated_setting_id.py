#by chatGPT
import os
import json

# 定义一个空列表，用于存储所有的 setting_id
setting_id_values = []

# 定义递归函数
def traverse_files(path):
    for file in os.listdir(path):
        file_path = os.path.join(path, file)
        if os.path.isdir(file_path):
            traverse_files(file_path)  # 递归遍历子文件夹
        elif file_path.endswith('.json'):
            # 解析 JSON 文件并提取 setting_id 的值
            with open(file_path) as f:
                try:
                    data = json.load(f)
                    if 'setting_id' in data:
                        setting_id = data['setting_id']
                        if isinstance(setting_id, str):
                            setting_id_values.append(setting_id)
                            # print(f"Found setting_id value: {setting_id}")
                except (KeyError, json.JSONDecodeError):
                    pass

# 从当前目录开始遍历
traverse_files('.')

from collections import Counter

# 统计每个 setting_id 出现的次数
setting_id_counts = Counter(setting_id_values)

# 找出出现次数大于 1 的 setting_id
duplicated_setting_ids = [setting_id for setting_id, count in setting_id_counts.items() if count > 1]

# 输出重复的 setting_id
if len(duplicated_setting_ids) > 0:
    print("Found duplicated setting_id values:")
    for setting_id in duplicated_setting_ids:
        print(setting_id)
else:
    print("No duplicated setting_id values.")