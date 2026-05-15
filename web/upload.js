// Module upload handler for /modules page
document.addEventListener('DOMContentLoaded', () => {
  const form = document.getElementById('upload-form');
  if (!form) return;

  const progress = document.getElementById('upload-progress');
  const status   = document.getElementById('upload-status');

  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const file = form.querySelector('input[type=file]').files[0];
    if (!file) return;

    if (!file.name.endsWith('.ctm')) {
      status.textContent = 'Error: file must be a .ctm bundle.';
      status.style.color = '#f85149';
      return;
    }

    status.textContent = 'Uploading…';
    status.style.color = '#79c0ff';
    if (progress) progress.value = 0;

    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/modules/upload');

    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable && progress) {
        progress.value = Math.round((e.loaded / e.total) * 100);
      }
    };

    xhr.onload = () => {
      if (xhr.status === 200) {
        status.textContent = 'Upload successful. Module installed.';
        status.style.color = '#3fb950';
        setTimeout(() => window.location.reload(), 1500);
      } else {
        status.textContent = 'Upload failed: ' + xhr.responseText;
        status.style.color = '#f85149';
      }
    };

    xhr.onerror = () => {
      status.textContent = 'Network error during upload.';
      status.style.color = '#f85149';
    };

    const data = new FormData();
    data.append('ctm', file);
    xhr.send(data);
  });
});
