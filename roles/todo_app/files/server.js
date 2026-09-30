const express = require('express');
const cors = require('cors');

const app = express();
const PORT = process.env.PORT || 3001;

// ─── Middleware ──────────────────────────────────────────────────────────────
app.use(cors());
app.use(express.json());

// Logger de requisições HTTP para stdout (coletado pelo Promtail/Loki)
app.use((req, res, next) => {
  const start = Date.now();
  res.on('finish', () => {
    const duration = Date.now() - start;
    console.log(`[HTTP] ${req.method} ${req.originalUrl} -> ${res.statusCode} (${duration}ms)`);
  });
  next();
});

// ─── In-memory store ─────────────────────────────────────────────────────────
let todos = [];

// ─── Routes ──────────────────────────────────────────────────────────────────

// GET /api/todos — retorna todos os itens
app.get('/api/todos', (req, res) => {
  res.json({ success: true, data: todos });
});

// POST /api/todos — cria novo item
app.post('/api/todos', (req, res) => {
  const { text } = req.body;

  if (!text || typeof text !== 'string' || text.trim() === '') {
    console.warn('[CRUD] [CREATE] Falha: campo "text" vazio ou inválido.');
    return res.status(400).json({ success: false, message: 'O campo "text" é obrigatório.' });
  }

  const newTodo = {
    id: Date.now().toString(),
    text: text.trim(),
    completed: false,
    createdAt: new Date().toISOString(),
  };

  todos.push(newTodo);
  console.log(`[CRUD] [CREATE] Nova tarefa criada | id: ${newTodo.id} | texto: "${newTodo.text}"`);
  res.status(201).json({ success: true, data: newTodo });
});

// PATCH /api/todos/:id — alterna o status de completado
app.patch('/api/todos/:id', (req, res) => {
  const { id } = req.params;
  const todo = todos.find((t) => t.id === id);

  if (!todo) {
    console.warn(`[CRUD] [UPDATE] Tarefa não encontrada: id=${id}`);
    return res.status(404).json({ success: false, message: 'Tarefa não encontrada.' });
  }

  todo.completed = !todo.completed;
  console.log(`[CRUD] [UPDATE] Tarefa atualizada | id: ${todo.id} | concluída: ${todo.completed} | texto: "${todo.text}"`);
  res.json({ success: true, data: todo });
});

// DELETE /api/todos/:id — remove um item
app.delete('/api/todos/:id', (req, res) => {
  const { id } = req.params;
  const index = todos.findIndex((t) => t.id === id);

  if (index === -1) {
    console.warn(`[CRUD] [DELETE] Tarefa não encontrada: id=${id}`);
    return res.status(404).json({ success: false, message: 'Tarefa não encontrada.' });
  }

  const [removed] = todos.splice(index, 1);
  console.log(`[CRUD] [DELETE] Tarefa removida | id: ${removed.id} | texto: "${removed.text}"`);
  res.json({ success: true, data: removed });
});

// DELETE /api/todos — remove todos os itens concluídos
app.delete('/api/todos', (req, res) => {
  const before = todos.length;
  todos = todos.filter((t) => !t.completed);
  const removedCount = before - todos.length;
  console.log(`[CRUD] [DELETE_ALL] Tarefas concluídas removidas: ${removedCount} | restantes: ${todos.length}`);
  res.json({ success: true, removed: removedCount });
});

// PUT /api/todos/sync — sincronização entre frontend e backend
app.put('/api/todos/sync', (req, res) => {
  const { todos: clientTodos } = req.body;
  if (!Array.isArray(clientTodos)) {
    console.warn('[CRUD] [SYNC] Payload inválido recebido.');
    return res.status(400).json({ success: false, message: 'Payload inválido.' });
  }
  todos = clientTodos;
  console.log(`[CRUD] [SYNC] Sincronização realizada | total de tarefas no backend: ${todos.length}`);
  res.json({ success: true, data: todos });
});

// GET / — rota raiz da API
app.get('/', (req, res) => {
  res.json({ success: true, message: 'Todo API operacional', port: PORT });
});

// 404 handler — rota não encontrada (retorna JSON e evita ENOENT de index.html inexistente)
app.use((req, res) => {
  res.status(404).json({ success: false, message: `Rota ${req.method} ${req.url} não encontrada` });
});

// Error handler
app.use((err, req, res, next) => {
  console.error(`[ERROR] ${err.message}`);
  res.status(500).json({ success: false, message: 'Erro interno no servidor' });
});

// ─── Start ───────────────────────────────────────────────────────────────────
app.listen(PORT, () => {
  console.log(`\n  🚀 Todo API rodando em http://localhost:${PORT}`);
  console.log(`  📋 Endpoints:`);
  console.log(`     GET    /api/todos`);
  console.log(`     POST   /api/todos`);
  console.log(`     PATCH  /api/todos/:id`);
  console.log(`     DELETE /api/todos/:id`);
  console.log(`     DELETE /api/todos  (limpa concluídos)\n`);
});
