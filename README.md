# vg5k_dither
Convertisseur d'images vers VG5000

La conversion d'images modernes (JPEG, PNG, ...) vers le [Philips VG5000](https://fr.wikipedia.org/wiki/Philips_VG5000%C2%B5) est un exercice technique très particulier en raison du processeur graphique de la machine (le [SGS Thomson EF9345](https://en.wikipedia.org/wiki/Thomson_EF9345)). Ce composant gère l'affichage en mode semi-graphique (40 colonnes × 25 lignes). Il n'y a pas de mode "bitmap" pixel par pixel classique, l'image doit être convertie en une grille de caractères redéfinis (limités à 500 caractères personnalisables en RAM de 4*10 pixels en 4 couleurs sur une palette de 8).
