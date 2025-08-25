using UnityEngine;

public class ScooterController : MonoBehaviour
{
    [Header("Movement")]
    [SerializeField] private float moveSpeed = 8.0f; // движение вперёд к игроку (по -Z)
    [SerializeField] private float despawnZ = -10f; // если проехал мимо игрока и не уничтожен — промах
    [SerializeField] private int laneIndex = 1; // 0-левая, 1-центральная, 2-правая
    [SerializeField] private float laneOffset = 2.0f;

    [Header("Scoring")]
    [SerializeField] private int scorePerHit = 1;

    private bool wasResolved = false; // уже обработан (сбит/попадание/промах)

    private void Start()
    {
        // Привязка к полосе при спавне
        Vector3 pos = transform.position;
        pos.x = (laneIndex - 1) * laneOffset;
        transform.position = pos;
        gameObject.tag = "Scooter"; // гарантируем тег для столкновения с игроком
    }

    private void Update()
    {
        transform.Translate(Vector3.back * moveSpeed * Time.deltaTime, Space.World);

        if (!wasResolved && transform.position.z < despawnZ)
        {
            wasResolved = true;
            // Промах по самокату — конец игры
            ScoreManager.Instance?.TriggerGameOver();
            Destroy(gameObject);
        }
    }

    private void OnTriggerEnter(Collider other)
    {
        if (wasResolved) return;

        // Попадание атакой игрока
        if (other.CompareTag("PlayerAttack"))
        {
            wasResolved = true;
            ScoreManager.Instance?.AddScore(scorePerHit);
            Destroy(gameObject);
            return;
        }

        // Столкновение с игроком (резервный вариант, если игрок не обработал)
        if (other.CompareTag("Player"))
        {
            wasResolved = true;
            ScoreManager.Instance?.TriggerGameOver();
            Destroy(gameObject);
        }
    }
}

