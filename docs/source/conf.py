project = 'Gantry'
copyright = '2026, Demizo'
author = 'Demizo'
release = '1.0.0'

extensions = [
    'breathe',
    'sphinxcontrib.mermaid',
    'sphinx_rtd_theme',
]

breathe_projects = {
    "Firmware": "../doxygen/xml" 
}
breathe_default_project = "Firmware"

templates_path = ['_templates']
exclude_patterns = []

html_theme = 'sphinx_rtd_theme'
html_static_path = ['_static']
html_show_sphinx = False

html_context = {
    "show_copyright": True,
    "copyright": "2026, Demizo",
}