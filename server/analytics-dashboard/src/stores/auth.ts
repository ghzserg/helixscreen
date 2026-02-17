import { defineStore } from 'pinia'
import { computed, ref } from 'vue'

export const useAuthStore = defineStore('auth', () => {
  const apiKey = ref(sessionStorage.getItem('apiKey') || '')

  const isAuthenticated = computed(() => apiKey.value.length > 0)

  function login(key: string) {
    apiKey.value = key
    sessionStorage.setItem('apiKey', key)
  }

  function logout() {
    apiKey.value = ''
    sessionStorage.removeItem('apiKey')
  }

  return { apiKey, isAuthenticated, login, logout }
})
