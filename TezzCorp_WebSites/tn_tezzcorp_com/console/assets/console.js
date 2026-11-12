/**
 * console/assets/console.js — TezzNative Console Interactivity & Utilities
 */

document.addEventListener('DOMContentLoaded', function () {
  // 1. Mobile Sidebar Drawer & Overlay Management
  const mobileToggle = document.getElementById('mobileNavToggle');
  const sidebar = document.getElementById('consoleSidebar');
  const backdrop = document.getElementById('sidebarBackdrop');
  const closeBtn = document.getElementById('sidebarCloseBtn');

  function openSidebar() {
    if (sidebar) sidebar.classList.add('open');
    if (backdrop) backdrop.classList.add('active');
    document.body.style.overflow = 'hidden';
  }

  function closeSidebar() {
    if (sidebar) sidebar.classList.remove('open');
    if (backdrop) backdrop.classList.remove('active');
    document.body.style.overflow = '';
  }

  if (mobileToggle && sidebar) {
    mobileToggle.addEventListener('click', function (e) {
      e.stopPropagation();
      if (sidebar.classList.contains('open')) {
        closeSidebar();
      } else {
        openSidebar();
      }
    });
  }

  if (closeBtn) {
    closeBtn.addEventListener('click', function (e) {
      e.stopPropagation();
      closeSidebar();
    });
  }

  if (backdrop) {
    backdrop.addEventListener('click', function () {
      closeSidebar();
    });
  }

  // Auto-close sidebar on mobile nav link click
  if (sidebar) {
    sidebar.querySelectorAll('.sidebar-item').forEach(function (link) {
      link.addEventListener('click', function () {
        if (window.innerWidth <= 900) {
          closeSidebar();
        }
      });
    });
  }

  // 2. Modal attribute triggers
  document.querySelectorAll('[data-modal-open]').forEach(function (trigger) {
    trigger.addEventListener('click', function () {
      const modalId = trigger.getAttribute('data-modal-open');
      window.openModal(modalId);
    });
  });

  document.querySelectorAll('[data-modal-close], .modal-close').forEach(function (btn) {
    btn.addEventListener('click', function () {
      const modal = btn.closest('.c-modal-overlay, .modal-backdrop');
      if (modal) modal.classList.remove('active');
    });
  });

  // Close modal when clicking on backdrop
  document.querySelectorAll('.c-modal-overlay, .modal-backdrop').forEach(function (overlay) {
    overlay.addEventListener('click', function (e) {
      if (e.target === overlay) {
        overlay.classList.remove('active');
      }
    });
  });

  // Close on Escape key
  document.addEventListener('keydown', function (e) {
    if (e.key === 'Escape') {
      document.querySelectorAll('.c-modal-overlay.active, .modal-backdrop.active').forEach(function (m) {
        m.classList.remove('active');
      });
      closeSidebar();
    }
  });

  // 3. Dynamic Real-time Table Filter
  const filterInputs = document.querySelectorAll('#tableSearchInput, input[name="q"], .table-filter-input');
  filterInputs.forEach(function (input) {
    input.addEventListener('input', function () {
      const query = input.value.trim().toLowerCase();
      const rows = document.querySelectorAll('.filterable-row, .data-table tbody tr, .console-table tbody tr');
      rows.forEach(function (row) {
        // Skip empty message rows
        if (row.cells && row.cells.length === 1 && row.cells[0].colSpan > 1) return;
        const text = row.innerText.toLowerCase();
        row.style.display = text.includes(query) ? '' : 'none';
      });
    });
  });
});

// Global Modal Functions
window.openModal = function (modalId) {
  const modal = document.getElementById(modalId);
  if (modal) {
    modal.classList.add('active');
    const firstInput = modal.querySelector('input:not([type="hidden"]), select, textarea');
    if (firstInput) {
      setTimeout(() => firstInput.focus(), 80);
    }
  }
};

window.closeModal = function (modalId) {
  const modal = document.getElementById(modalId);
  if (modal) {
    modal.classList.remove('active');
  }
};

// Clipboard copy helper
function copyToClipboard(text, btn) {
  if (!navigator.clipboard) {
    const el = document.createElement('textarea');
    el.value = text;
    document.body.appendChild(el);
    el.select();
    document.execCommand('copy');
    document.body.removeChild(el);
    if (btn) {
      const orig = btn.innerText;
      btn.innerText = 'Copied!';
      setTimeout(function () { btn.innerText = orig; }, 1800);
    }
    return;
  }

  navigator.clipboard.writeText(text).then(function () {
    if (btn) {
      const orig = btn.innerText;
      btn.innerText = 'Copied!';
      setTimeout(function () { btn.innerText = orig; }, 1800);
    }
  });
}
