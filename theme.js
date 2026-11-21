(function() {
  try {
    var stored = localStorage.getItem('rapidaid_theme');
    var theme = stored;
    if (!theme) {
      if (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches) {
        theme = 'night';
      } else {
        theme = 'day';
      }
    }
    if (theme === 'night') {
      document.documentElement.dataset.theme = 'night';
    } else {
      delete document.documentElement.dataset.theme;
    }
  } catch (e) {}
})();
