<template>
  <div class="date-range-picker">
    <button
      v-for="opt in options"
      :key="opt.value"
      class="range-btn"
      :class="{ active: modelValue === opt.value }"
      @click="$emit('update:modelValue', opt.value)"
    >
      {{ opt.label }}
    </button>
  </div>
</template>

<script setup lang="ts">
defineProps<{
  modelValue: string
}>()

defineEmits<{
  'update:modelValue': [value: string]
}>()

const options = [
  { label: '7d', value: '7d' },
  { label: '30d', value: '30d' },
  { label: '90d', value: '90d' }
]
</script>

<style scoped>
.date-range-picker {
  display: inline-flex;
  gap: 0;
  border: 1px solid var(--border);
  border-radius: 6px;
  overflow: hidden;
}

.range-btn {
  padding: 6px 16px;
  background: transparent;
  border: none;
  border-right: 1px solid var(--border);
  color: var(--text-secondary);
  font-size: 13px;
  transition: color 0.15s, background 0.15s;
}

.range-btn:last-child {
  border-right: none;
}

.range-btn:hover {
  color: var(--text-primary);
  background: rgba(59, 130, 246, 0.08);
}

.range-btn.active {
  color: var(--accent-blue);
  background: rgba(59, 130, 246, 0.15);
  font-weight: 500;
}
</style>
