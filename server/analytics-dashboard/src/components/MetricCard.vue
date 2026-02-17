<template>
  <div class="metric-card">
    <div class="metric-title">{{ title }}</div>
    <div class="metric-value" :style="{ color: color || 'var(--accent-blue)' }">{{ value }}</div>
    <div v-if="subtitle" class="metric-subtitle">{{ subtitle }}</div>
  </div>
</template>

<script setup lang="ts">
defineProps<{
  title: string
  value: string | number
  subtitle?: string
  color?: string
}>()
</script>

<style scoped>
.metric-card {
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 20px;
}

.metric-title {
  font-size: 13px;
  color: var(--text-secondary);
  margin-bottom: 8px;
  text-transform: uppercase;
  letter-spacing: 0.05em;
}

.metric-value {
  font-size: 28px;
  font-weight: 700;
  line-height: 1.2;
}

.metric-subtitle {
  font-size: 12px;
  color: var(--text-secondary);
  margin-top: 4px;
}
</style>
