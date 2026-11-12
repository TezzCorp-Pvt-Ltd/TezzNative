/**
 * console/assets/console.js — TezzNative Console Interactivity
 */

document.addEventListener('DOMContentLoaded', function () {
  // 1. Mobile Sidebar Drawer
  const mobileToggle = document.getElementById('mobileNavToggle');
  const sidebar = document.getElementById('consoleSidebar');
  if (mobileToggle && sidebar) {
    mobileToggle.addEventListener('click', function () {
      sidebar.classList.toggle('open');
    });
    // Close sidebar on outside click
    document.addEventListener('click', function (e) {
      if (sidebar.classList.contains('open') && !sidebar.contains(e.target) && !mobileToggle.contains(e.target)) {
        sidebar.classList.remove('open');
      }
    });
  }

  // 2. Modals
  document.querySelectorAll('[data-modal-open]').forEach(function (trigger) {
    trigger.addEventListener('click', function () {
      const modalId = trigger.getAttribute('data-modal-open');
      const modal = document.getElementById(modalId);
      if (modal) modal.classList.add('active');
    });
  });

  document.querySelectorAll('[data-modal-close]').forEach(function (btn) {
    btn.addEventListener('click', function () {
      const modal = btn.closest('.c-modal-overlay');
      if (modal) modal.classList.remove('active');
    });
  });

  // Close modal on click background
  document.querySelectorAll('.c-modal-overlay').forEach(function (overlay) {
    overlay.addEventListener('click', function (e) {
      if (e.target === overlay) overlay.classList.remove('active');
    });
  });

  // 3. Dynamic Table Search Filter
  const tableSearchInput = document.getElementById('tableSearchInput');
  if (tableSearchInput) {
    tableSearchInput.addEventListener('input', function () {
      const query = tableSearchInput.value.trim().toLowerCase();
      const rows = document.querySelectorAll('.filterable-row');
      rows.forEach(function (row) {
        const text = row.innerText.toLowerCase();
        row.style.display = text.includes(query) ? '' : 'none';
      });
    });
  }
});

function copyToClipboard(text, btn) {
  navigator.clipboard.writeText(text).then(function () {
    if (btn) {
      const orig = btn.innerText;
      btn.innerText = 'Copied!';
      setTimeout(function () { btn.innerText = orig; }, 1800);
    }
  });
}
