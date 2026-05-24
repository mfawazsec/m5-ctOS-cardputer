// PIN gate: if /api/status returns 401, redirect to /auth
(async () => {
  try {
    const r = await fetch('/api/status');
    if (r.status === 401) {
      window.location.href = '/auth?next=' + encodeURIComponent(window.location.pathname);
    }
  } catch (_) {}
})();
