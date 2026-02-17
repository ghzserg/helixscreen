<template>
  <div class="login-page">
    <div class="login-card">
      <h1 class="login-title">HelixScreen Analytics</h1>
      <form @submit.prevent="handleLogin">
        <div class="form-group">
          <label for="apiKey">API Key</label>
          <input
            id="apiKey"
            v-model="apiKey"
            type="password"
            placeholder="Enter your admin API key"
            :disabled="loading"
          />
        </div>
        <div v-if="error" class="error-msg">{{ error }}</div>
        <button type="submit" class="login-btn" :disabled="loading || !apiKey">
          {{ loading ? 'Validating...' : 'Login' }}
        </button>
      </form>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref } from 'vue'
import { useAuthStore } from '@/stores/auth'
import { api } from '@/services/api'
import { router } from '@/router'

const auth = useAuthStore()
const apiKey = ref('')
const error = ref('')
const loading = ref(false)

async function handleLogin() {
  error.value = ''
  loading.value = true
  try {
    auth.login(apiKey.value)
    await api.getOverview('30d')
    router.push('/')
  } catch (e) {
    auth.logout()
    if (e instanceof Error && e.message === 'Unauthorized') {
      error.value = 'Invalid API key'
    } else {
      error.value = 'Cannot reach server — check connection'
    }
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
.login-page {
  height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--bg-main);
}

.login-card {
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: 12px;
  padding: 40px;
  width: 100%;
  max-width: 400px;
}

.login-title {
  font-size: 20px;
  font-weight: 600;
  margin-bottom: 28px;
  text-align: center;
  color: var(--text-primary);
}

.form-group {
  margin-bottom: 16px;
}

.form-group label {
  display: block;
  font-size: 13px;
  color: var(--text-secondary);
  margin-bottom: 6px;
}

.form-group input {
  width: 100%;
  padding: 10px 12px;
  background: var(--bg-main);
  border: 1px solid var(--border);
  border-radius: 6px;
  color: var(--text-primary);
  font-size: 14px;
  outline: none;
  transition: border-color 0.15s;
}

.form-group input:focus {
  border-color: var(--accent-blue);
}

.error-msg {
  color: var(--accent-red);
  font-size: 13px;
  margin-bottom: 12px;
}

.login-btn {
  width: 100%;
  padding: 10px;
  background: var(--accent-blue);
  border: none;
  border-radius: 6px;
  color: white;
  font-size: 14px;
  font-weight: 500;
  transition: opacity 0.15s;
}

.login-btn:hover:not(:disabled) {
  opacity: 0.9;
}

.login-btn:disabled {
  opacity: 0.5;
}
</style>
