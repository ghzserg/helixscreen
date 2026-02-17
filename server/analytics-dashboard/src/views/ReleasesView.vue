<template>
  <AppLayout>
    <div class="page">
      <div class="page-header">
        <h2>Releases</h2>
      </div>

      <div class="version-selector">
        <h3>Select versions to compare</h3>
        <div class="version-pills">
          <button
            v-for="v in availableVersions"
            :key="v"
            class="version-pill"
            :class="{ active: selectedVersions.includes(v) }"
            @click="toggleVersion(v)"
          >
            {{ v }}
          </button>
        </div>
      </div>

      <div v-if="loading" class="loading">Loading...</div>
      <div v-else-if="error" class="error">{{ error }}</div>
      <div v-else-if="data" class="releases-grid">
        <div v-for="ver in data.versions" :key="ver.version" class="release-card">
          <h3 class="release-version">{{ ver.version }}</h3>
          <div class="release-stats">
            <div class="stat">
              <span class="stat-label">Active Devices</span>
              <span class="stat-value">{{ ver.active_devices }}</span>
            </div>
            <div class="stat">
              <span class="stat-label">Sessions</span>
              <span class="stat-value">{{ ver.total_sessions.toLocaleString() }}</span>
            </div>
            <div class="stat">
              <span class="stat-label">Crash Rate</span>
              <span class="stat-value" :style="{ color: ver.crash_rate * 100 > 5 ? 'var(--accent-red)' : 'var(--accent-green)' }">
                {{ (ver.crash_rate * 100).toFixed(1) }}%
              </span>
            </div>
            <div class="stat">
              <span class="stat-label">Print Success</span>
              <span class="stat-value" style="color: var(--accent-green)">
                {{ (ver.print_success_rate * 100).toFixed(1) }}%
              </span>
            </div>
            <div class="stat">
              <span class="stat-label">Crashes</span>
              <span class="stat-value" style="color: var(--accent-red)">
                {{ ver.total_crashes }}
              </span>
            </div>
          </div>
        </div>
      </div>
      <div v-else class="empty">Select versions above to compare</div>
    </div>
  </AppLayout>
</template>

<script setup lang="ts">
import { ref, watch } from 'vue'
import AppLayout from '@/components/AppLayout.vue'
import { api } from '@/services/api'
import type { ReleasesData } from '@/services/api'

const availableVersions = ref<string[]>([])
const selectedVersions = ref<string[]>([])
const data = ref<ReleasesData | null>(null)
const loading = ref(false)
const error = ref('')

function toggleVersion(v: string) {
  const idx = selectedVersions.value.indexOf(v)
  if (idx >= 0) {
    selectedVersions.value.splice(idx, 1)
  } else {
    selectedVersions.value.push(v)
  }
}

async function fetchVersionList() {
  try {
    const adoption = await api.getAdoption('90d')
    availableVersions.value = adoption.versions.map(v => v.name)
  } catch {
    // Silently fail - versions will just be empty
  }
}

async function fetchReleases() {
  if (selectedVersions.value.length === 0) {
    data.value = null
    return
  }
  loading.value = true
  error.value = ''
  try {
    data.value = await api.getReleases(selectedVersions.value)
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to load data'
  } finally {
    loading.value = false
  }
}

watch(selectedVersions, fetchReleases, { deep: true })
fetchVersionList()
</script>

<style scoped>
.page-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 24px;
}

.page-header h2 {
  font-size: 20px;
  font-weight: 600;
}

.version-selector {
  margin-bottom: 24px;
}

.version-selector h3 {
  font-size: 14px;
  font-weight: 500;
  color: var(--text-secondary);
  margin-bottom: 12px;
}

.version-pills {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}

.version-pill {
  padding: 6px 14px;
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: 20px;
  color: var(--text-secondary);
  font-size: 13px;
  transition: all 0.15s;
}

.version-pill:hover {
  border-color: var(--accent-blue);
  color: var(--text-primary);
}

.version-pill.active {
  background: rgba(59, 130, 246, 0.15);
  border-color: var(--accent-blue);
  color: var(--accent-blue);
}

.releases-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 16px;
}

.release-card {
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 20px;
}

.release-version {
  font-size: 16px;
  font-weight: 600;
  margin-bottom: 16px;
  color: var(--accent-blue);
}

.release-stats {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.stat {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.stat-label {
  font-size: 13px;
  color: var(--text-secondary);
}

.stat-value {
  font-size: 15px;
  font-weight: 600;
  color: var(--text-primary);
}

.loading, .error, .empty {
  padding: 40px;
  text-align: center;
  color: var(--text-secondary);
}

.error {
  color: var(--accent-red);
}
</style>
