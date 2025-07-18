#!/bin/bash

# 脚本说明：每3秒尝试推送至 origin/master，直到成功
# 日志文件：auto_push.log（记录每次尝试时间及结果）

while true; do
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] 尝试推送至 master..."
    git push origin master

    # 检查推送结果
    if [ $? -eq 0 ]; then
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] ✅ 推送成功！"
        break  # 成功则退出循环
    else
        echo "[$(date '+%Y-%m:%d %H:%M:%S')] ❌ 推送失败，3秒后重试..."
        sleep 3
    fi
done