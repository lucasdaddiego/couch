# The Worker in a throwaway container (repo JS runs only in a container here, from a
# git archive, never a bind mount). dev.zsh builds it, runs it on the LAN, and removes it.
FROM node:22-bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates && rm -rf /var/lib/apt/lists/*
ENV WRANGLER_SEND_METRICS=false CI=true
WORKDIR /w
ADD worker.tar /w/
RUN npm ci --no-audit --no-fund
EXPOSE 8787
CMD ["npx", "wrangler", "dev", "--ip", "0.0.0.0", "--port", "8787"]
