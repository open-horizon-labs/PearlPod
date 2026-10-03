FROM python:3.12-slim
RUN apt-get update && apt-get install -y --no-install-recommends ffmpeg lftp && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY syncer/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY syncer/*.py ./
USER 1000:1000
ENTRYPOINT ["python", "server.py"]
