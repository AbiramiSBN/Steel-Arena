const card = document.querySelector('.hero-card');
window.addEventListener('mousemove', (e) => {
  if (!card || window.innerWidth < 900) return;
  const x = (e.clientX / window.innerWidth - 0.5) * 8;
  const y = (e.clientY / window.innerHeight - 0.5) * -8;
  card.style.transform = `perspective(900px) rotateY(${x - 6}deg) rotateX(${y + 3}deg)`;
});
