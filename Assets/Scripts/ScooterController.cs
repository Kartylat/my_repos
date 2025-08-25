using UnityEngine;

public class ScooterController : MonoBehaviour
{
    [Header("Movement")]
    [SerializeField] private float moveSpeed = 8.0f; // forward towards negative Z or player Z
    [SerializeField] private float despawnZ = -10f; // if passes player and not destroyed -> miss
    [SerializeField] private int laneIndex = 1; // 0-left, 1-middle, 2-right
    [SerializeField] private float laneOffset = 2.0f;

    [Header("Scoring")]
    [SerializeField] private int scorePerHit = 1;

    private bool wasResolved = false; // hit or caused game over or missed

    private void Start()
    {
        // Snap to lane at spawn
        Vector3 pos = transform.position;
        pos.x = (laneIndex - 1) * laneOffset;
        transform.position = pos;
        gameObject.tag = "Scooter"; // ensure tag for player collision
    }

    private void Update()
    {
        transform.Translate(Vector3.back * moveSpeed * Time.deltaTime, Space.World);

        if (!wasResolved && transform.position.z < despawnZ)
        {
            wasResolved = true;
            // Missed scooter -> game over
            ScoreManager.Instance?.TriggerGameOver();
            Destroy(gameObject);
        }
    }

    private void OnTriggerEnter(Collider other)
    {
        if (wasResolved) return;

        // Hit by player's attack
        if (other.CompareTag("PlayerAttack"))
        {
            wasResolved = true;
            ScoreManager.Instance?.AddScore(scorePerHit);
            Destroy(gameObject);
            return;
        }

        // If we collide with player (backup in case player doesn't handle it)
        if (other.CompareTag("Player"))
        {
            wasResolved = true;
            ScoreManager.Instance?.TriggerGameOver();
            Destroy(gameObject);
        }
    }
}

