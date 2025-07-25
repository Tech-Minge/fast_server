import pandas as pd
import numpy as np
from bokeh.plotting import figure, show, output_file
from bokeh.models import ColumnDataSource, HoverTool, Span
from bokeh.palettes import Category20_20
from bokeh.layouts import column
from bokeh.models.widgets import Div

# 1. 从CSV文件读取数据
df = pd.read_csv('data.csv')  # 替换为你的CSV文件路径

# 2. 设置分组阈值（微秒）
THRESHOLD = 5  # 根据数据特性调整

# 3. 分组算法
def group_by_clock_diff(df, threshold):
    """根据连续行的clock差值小于阈值进行分组"""
    # 计算相邻行的clock差值
    df['clock_diff'] = df['clock'].diff().fillna(threshold + 1)
    
    # 创建分组标识
    group_id = 0
    df['group'] = group_id
    
    # 遍历数据行进行分组
    for i in range(1, len(df)):
        if df.at[i, 'clock_diff'] < threshold:
            df.at[i, 'group'] = group_id
        else:
            group_id += 1
            df.at[i, 'group'] = group_id
    
    # 添加组统计信息
    df['group_size'] = df.groupby('group')['id'].transform('count')
    df['group_duration'] = df.groupby('group')['clock'].transform(lambda x: x.max() - x.min())
    
    return df

# 执行分组
df = group_by_clock_diff(df, THRESHOLD)

# 4. 准备可视化
output_file("clock_groups.html")

# 为不同组分配颜色
groups = df['group'].unique()
colors = {group: Category20_20[i % len(Category20_20)] for i, group in enumerate(groups)}
df['color'] = df['group'].map(colors)
source = ColumnDataSource(df)

# 5. 创建主图表
p1 = figure(title=f"Clock Group Analysis (Threshold: {THRESHOLD}μs)", 
            width=1200, height=500, tools="pan,wheel_zoom,box_zoom,reset,save")
p1.scatter('clock', 'value', source=source, size=8, color='color', legend_field='group')

# 添加连接线
for group_id, group_df in df.groupby('group'):
    if len(group_df) > 1:
        p1.line(x=group_df['clock'], y=group_df['value'], color=colors[group_id], alpha=0.5)

# 6. 创建组持续时间图表
group_stats = df.groupby('group').agg(
    duration=('clock', lambda x: x.max() - x.min()),
    size=('id', 'count')
).reset_index()
group_stats['color'] = group_stats['group'].map(colors)
group_stats_source = ColumnDataSource(group_stats)

p2 = figure(title="Group Duration", width=1200, height=300, 
            x_axis_label='Group ID', y_axis_label='Duration (μs)')
p2.vbar(x='group', top='duration', width=0.8, source=group_stats_source,
        fill_color='color', line_color="black")

# 7. 添加标题和说明
title_text = f"""
<h1>Clock Group Analysis</h1>
<p>Total Points: {len(df)} | Groups: {len(groups)} | Threshold: {THRESHOLD}μs</p>
<p>Min Duration: {group_stats['duration'].min()}μs | Max Duration: {group_stats['duration'].max()}μs</p>
"""
title = Div(text=title_text, width=1200)

# 8. 显示图表
show(column(title, p1, p2))